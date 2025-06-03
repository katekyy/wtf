#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wctype.h>

#include "config.h"
#include "cvector.h"

#define TB_IMPL
#include "termbox2.h"

typedef struct
{
  char *label;
  size_t label_sz;
  int distance;
  int inaccuracy;
  bool *markers;
}
wtf_entry_t;

wtf_entry_t
wtf_entry_new(char *label, size_t lsz)
{
  bool *markers = malloc(lsz);
  memset(markers, false, lsz);

  return (wtf_entry_t){
    .label = label,
    .label_sz = lsz,
    .distance = 0,
    .inaccuracy = 0,
    .markers = markers,
  };
}

void
str_rate_free(wtf_entry_t *entry)
{
  free(entry->markers);
}

#define eq_case_insensitive(a, b) \
  (towlower((a)) == towlower((b)))

int
minimum(int x, int y)
{
  return x < y ? x : y;
}

int
minimum3(int x, int y, int z)
{
  return x < y ? minimum(x, z) : minimum(y, z);
}

/*
 * Levenshtein distance.
 * Implemented from Wikipedia.org: https://en.wikipedia.org/wiki/Levenshtein_distance
 *
 * Tweaked array indexing though.
 */
int
ldistance(char* a, size_t asz, char* b, size_t bsz)
{
  int d[asz+1][bsz+1];
  memset(d, 0, (asz+1 * sizeof(int)) * (bsz+1 * sizeof(int)));

  for (size_t i = 0; i < asz; i++) d[i][0] = i;
  for (size_t i = 0; i < bsz; i++) d[0][i] = i;

  for (size_t j = 0; j < bsz; j++)
  {
    for (size_t i = 0; i < asz; i++)
    {
      int cost = (a[i] == b[j]) ? 0 : 1;

      d[i+1][j+1] = minimum3(
        d[i][j+1] + 1,
        d[i+1][j] + 1,
        d[i][j] + cost
      );
    }
  }

  return d[asz][bsz];
}

/*
 * TODO: Document/explain this algorithm.
 */
void
wtf_entry_rate(wtf_entry_t *entry, char *pat, size_t pat_sz)
{
  char *l = entry->label;
  size_t lsz = entry->label_sz;
  bool *marks = entry->markers;

  ssize_t most_distant_marker = -1;

  /* Clear old match markers. */
  memset(marks, false, lsz);

  entry->inaccuracy = pat_sz;

  size_t j = 0; /* Index in pattern. */
  for (size_t i = 0; i < lsz && j < pat_sz; i++)
  {
    if (eq_case_insensitive(l[i], pat[j]))
    {
      if (most_distant_marker < 0) most_distant_marker = i;
      marks[i] = true; /* Mark matched character. */
      entry->inaccuracy--;
      j++;
    }
  }

  entry->inaccuracy += (pat_sz - j);
  entry->distance = ldistance(
    l, lsz,
    pat, pat_sz
  ) + most_distant_marker + entry->inaccuracy;
}

int
wtf_entry_cmp(const wtf_entry_t **a, const wtf_entry_t **b)
{
  return (*a)->distance - (*b)->distance;
}

/* Calculates Y coordinate from the bottom or top of the terminal. */
#ifdef DIRECTION_TOP
#define calcy(y) (y)
#endif

#ifdef DIRECTION_BTM
#define calcy(y) (tb_height() - (y + 1))
#endif

void
scroll_to_fit(size_t *scroll, size_t selected, size_t max_visible)
{
  if (selected < *scroll)
  {
    *scroll = selected;
  }
  else if (selected >= *scroll + max_visible)
  {
    *scroll = selected - max_visible + 1;
  }
}

#define copy_item_refs(src, dst)         \
  do {                                   \
    size_t src_sz = cvector_size((src)); \
    cvector_reserve((dst), src_sz);      \
    for (size_t i = 0; i < src_sz; i++)  \
      (dst)[i] = &(src)[i];              \
    cvector_set_size((dst), src_sz);     \
  } while (0)

wtf_entry_t*
finder_start(cvector(wtf_entry_t) *list)
{
  {
    int tb_status = tb_init();
    if (tb_status)
    {
      fprintf(stderr, "initializing termbox failed with code %d\n", tb_status);
      return NULL;
    }
  }

  tb_set_input_mode(TB_INPUT_ESC | TB_INPUT_MOUSE);
  tb_set_cursor(0, calcy(0));

  struct tb_event ev;

  /* The entry we return. */
  wtf_entry_t *entry = NULL;

  wtf_entry_t **filtered = NULL;

  char *query = NULL;
  size_t cursor = 0;

  size_t max_visible = tb_height() - 2;
  size_t selected = 0;
  size_t scroll = 0;

  /*
   * We set the destructor to NULL, and we copy references to items in `list`.
   */
  cvector_init(filtered, 255, NULL);
  copy_item_refs(*list, filtered);

  cvector_init(query, 32, NULL);

  do
  {
    if (cvector_size(filtered) == 0)
    {
      /* Go to the beginning if we get no matches and then we get matches again instead of going at the end of the list. */
      selected = 0;
      scroll = 0;
    }
    else
    {
      /*
       * Reset scroll and select the last visible item if `filtered` shrank below the selector.
       * Then, just adjust scroll again to fix selector going out of sight.
       */
      if (selected >= cvector_size(filtered))
      {
        selected = cvector_size(filtered) - 1;
        scroll = 0;
      }
      scroll_to_fit(&scroll, selected, max_visible);
    }

    /*
     * DRAWING
     */
    tb_clear();
    {
      /* Print query and set the cursor at the end of it. */
      tb_print(0, calcy(0), QUERY_PREFIX_COLOR, TB_DEFAULT, QUERY_PREFIX);
      tb_printf(QUERY_PREFIX_SZ + 1, calcy(0), TB_DEFAULT, TB_DEFAULT, "%.*s", cvector_size(query), query);
      tb_set_cursor(QUERY_PREFIX_SZ + 1 + cursor, calcy(0));

      /*
       * Draw the status bar:
       * - L/A -------------------------------------------
       * Where:
       *   L -> number of listed entries
       *   A -> number of all entries
       */
      {
        size_t w = 0;

        tb_printf_ex(
          0,
          calcy(1),
          STATUS_BAR_COLOR,
          TB_DEFAULT,
          &w,
          "%s %ld/%ld",
          STATUS_BAR_FILL,
          cvector_size(filtered),
          cvector_size(*list)
        );

        const size_t remaining_dashes = tb_width();
        for (size_t i = (w + 1); i < remaining_dashes; i += STATUS_BAR_FILL_SZ)
          tb_print(i, calcy(1), STATUS_BAR_COLOR, TB_DEFAULT, STATUS_BAR_FILL);
      }

      /* Draw the filtered list. */
      size_t visible = cvector_size(filtered);
      if (visible > max_visible) visible = max_visible;

      for (size_t i = 0; i < visible; i++)
      {
        size_t real_idx = scroll + i;
        wtf_entry_t *item = filtered[real_idx];
        size_t primary_fg_attr = TB_DEFAULT;

        if (real_idx == selected)
        {
          tb_print(0, calcy(2 + i), SELECTOR_COLOR, TB_DEFAULT, SELECTOR);
          primary_fg_attr |= TB_BOLD;
        }

        for (size_t j = 0; j < item->label_sz; j++)
        {
          size_t fg_attr = primary_fg_attr;
          if (item->markers[j]) fg_attr |= TB_RED | TB_BOLD;
          tb_set_cell(SELECTOR_SZ + 1 + j, calcy(2 + i), item->label[j], fg_attr, TB_DEFAULT);
        }
      }
    }
    tb_present();

    /*
     * EVENT LOGIC
     */
    tb_poll_event(&ev);

    if (ev.type == TB_EVENT_RESIZE)
    {
      max_visible = tb_height() - 2;
      scroll = 0; /* Reset scroll after resizing. */
      scroll_to_fit(&scroll, selected, max_visible);
    }

    if (ev.key == TB_KEY_ESC) goto start_finder_cleanup;
    if (ev.type == TB_EVENT_KEY)
    {
      bool query_update = false;

      switch (ev.key)
      {
        case 0:
          cvector_insert(query, cursor, ev.ch);
          cursor++;
          query_update = true;
          break;

        case TB_KEY_BACKSPACE:
        case TB_KEY_BACKSPACE2:
          if (cvector_size(query) > 0 && cursor > 0)
          {
            cursor--;
            cvector_erase(query, cursor);
            query_update = true;
          }
          break;

        case TB_KEY_ARROW_LEFT:
          cursor -= (cursor ? 1 : 0);
          break;

        case TB_KEY_ARROW_RIGHT:
          cursor += ((cursor < cvector_size(query)) ? 1 : 0);
          break;

        case TB_KEY_CTRL_A:
          cursor = 0;
          break;

        case TB_KEY_CTRL_E:
          cursor = cvector_size(query);
          break;

        case TB_KEY_ARROW_UP:
          if (cvector_size(filtered))
          {

#ifdef DIRECTION_TOP
            if (selected > 0) selected--;
            else selected = cvector_size(filtered) - 1;
#else /* DIRECTION_TOP */
            selected = (selected + 1) % cvector_size(filtered);
#endif /* DIRECTION_TOP */

            scroll_to_fit(&scroll, selected, max_visible);
          }
          break;

        case TB_KEY_ARROW_DOWN:
          if (cvector_size(filtered))
          {

#ifdef DIRECTION_TOP
            selected = (selected + 1) % cvector_size(filtered);
#else /* DIRECTION_TOP */
            if (selected > 0) selected--;
            else selected = cvector_size(filtered) - 1;
#endif /* DIRECTION_TOP */

            scroll_to_fit(&scroll, selected, max_visible);
          }
          break;

        case TB_KEY_ENTER:
          entry = (cvector_size(filtered) > 0) ? filtered[selected] : NULL;
          goto start_finder_cleanup;
      }

      if (query_update)
      {
        /*
         * Recompute the query when it's not empy.
         * If it is empty, clear markers and list all entries.
         */
        size_t query_sz = cvector_size(query);

        if (query_sz > 0)
        {
          /* We don't need to worry about destroying the strings. */
          cvector_set_size(filtered, 0);
          for (size_t i = 0; i < cvector_size(*list); i++) {
            wtf_entry_rate(&(*list)[i], query, query_sz);
            if ((*list)[i].inaccuracy <= FUZZ_MAX_INACCURACY) cvector_push_back(filtered, &(*list)[i]);
          }

          qsort(
            filtered,
            cvector_size(filtered),
            sizeof(wtf_entry_t*),
            (int (*)(const void*, const void*))wtf_entry_cmp
          );
        }
        else
        {
          /* Clear match markers - since we aren't matching against anything. */
          for (size_t i = 0; i < cvector_size(*list); i++)
            memset(filtered[i]->markers, false, filtered[i]->label_sz);
          copy_item_refs(*list, filtered);
        }
      }
    }
  }
  while (true);

start_finder_cleanup:
    cvector_free(query);
    cvector_free(filtered);

    tb_shutdown();

    return entry;
}

size_t
read_to_vec(cvector(char) *vec, size_t buf_size, FILE *stream)
{
  size_t read = 0;
  size_t i = 0;

  do
  {
    if (i + buf_size > cvector_capacity(*vec))
      cvector_reserve(*vec, cvector_capacity(*vec) * 2);

    read = fread((char*)(*vec + i), sizeof(char), buf_size, stream);
    i += read;
    cvector_set_size(*vec, i);
  }
  while (read);

  return i;
}

void
print_help(FILE *stream)
{
#define HELP \
  "Usage: wtf [OPTIONS]\n" \
  "\n" \
  "Simple interactive command line fuzzy finder.\n" \
  "Designed to take any kind of new-line separated list from STDIN.\n" \
  "\n" \
  "Options:\n" \
  "  -h, --help     display this help and exit\n" \
  "\n"

  fprintf(stream, HELP);
}

#define push_entry(list, buf, offset, size) \
  do { \
    char *entry = (char*)((buf) + (offset)); \
    cvector_push_back((list), str_rate_init((char*)((buf) + (offset)), (size))); \
    entry[(size)] = '\0'; \
  } while (0)

int
main(int argc, char **argv)
{
  if (argc > 1)
  {
    if (strcmp(argv[1], "--help") == 0
        || strcmp(argv[1], "-h") == 0)
    {
      print_help(stdout);
      return 0;
    }
    else
    {
      print_help(stderr);
      return 2;
    }
  }

  if (isatty(STDIN_FILENO))
  {
    fprintf(stderr, "wtf: expected piped input\n");
    return 2;
  }

  int err = 0;
  char *buf = NULL;
  wtf_entry_t *list = NULL;

  cvector_init(buf, 512, NULL);
  cvector_init(list, 64, NULL);

  read_to_vec(&buf, 255, stdin);

  /*
   * Split entries by newlines.
   */
  {
    size_t offset = 0;
    size_t size = 0;

    for (size_t i = 0; i < cvector_size(buf); i++)
    {
      char *ch = &buf[i];

      if (*ch == '\n')
      {
        if (size > 0)
        {
          cvector_push_back(
            list,
            wtf_entry_new((char*)(buf + offset), size)
          );
          offset += size;
          size = 0;
        }
        offset++;

        // Null terminate just in case...
        *ch = '\0';
      }
      else size++;
    }

    if (size > 0)
      cvector_push_back(
        list,
        wtf_entry_new((char*)(buf + offset), size)
      );
  }

  wtf_entry_t *entry = finder_start(&list);
  if (entry)
  {
    printf("%.*s\n", (int)entry->label_sz, entry->label);
  }
  else
  {
    err = 1;
  }

  cvector_set_elem_destructor(list, (void (*)(void*))str_rate_free);
  cvector_free(list);
  cvector_free(buf);

  return err;
}

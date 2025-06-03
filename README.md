## what the fuzz?

`wtf` is a minimal and lightweight fuzzy finder for the terminal, designed to be a simpler alternative to tools like, for example, `fzf`.
It performs fast incremental filtering and sorting of input lines using my own fuzzy matching algorithm
and [Levenshtein distance], and displays results in an interactive TUI list.

### Features

* Interactive terminal UI based on termbox2

* Case-insensitive fuzzy matching

* Scrollable, selectable list of matches

* Minimal memory usage

* Ideal for piping input and quickly selecting an entry

### Usage

```sh
rm -i `ls | wtf`
```

or

```sh
`ls /bin | wtf`
```

* __Input__: Takes lines from stdin (piped input).
* __Output__: Prints the selected line to stdout.
* __Controls__:
  * Type to filter results
  * Arrow keys to navigate matches
  * Enter to select
  * Esc to quit without selection
  * Supports Emacs-style keybindings (Only Ctrl-A and Ctrl-E for now)

### Building

Requires Make and any (I think) C compiler.

```sh
make
```

You can modify `config.h` to customize TUI colors and behavior.

### How It Works?

* Reads all input lines into memory at startup.

* Implements fuzzy matching with [Levenshtein distance] and a simple scoring algorithm tracking character matches and their order.

* Uses __termbox2__ for terminal UI handling and rendering.

* Incrementally filters and sorts the list based on query string with live updates.

### TODO

* _Maybeeee_ add command-line options for customization.

[Levenshtein distance]: https://en.wikipedia.org/wiki/Levenshtein_distance

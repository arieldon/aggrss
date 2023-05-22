# aggrss

aggrss is a graphical RSS/Atom feed aggregator for Linux.


## Compile and Install

aggrss depends on SDL2, FreeType, OpenGL, SQLite3, and OpenSSL. It also relies
on the standard C library and POSIX as well, primarly for threading.

Run the following command on Ubuntu to install the dependencies.

```
# apt install libsdl2-dev libfreetype-dev libgl-dev libssl-dev libsqlite3-dev build-essential
```

Then, run the install script.

```
$ ./install.sh
```


## Resources and References

- General
  - [Allen Webster's Template](https://git.mr4th.com/Mr4th/mr4th.git/)
    - I referenced this heavily for font rendering.
    - Allen also provides brief and clear explanations of different parts of
    his codebase on [YouTube](https://www.youtube.com/@Mr4thProgramming). Plus,
    he posts some fun instrumental soundtracks occassionally.

- Data Structures and Shared Memory
  - [Chris Wellon's null program](https://nullprogram.com/)
    - I referenced two blog posts for this project in particular.
      1. [C11 Lock-free Stack](https://nullprogram.com/blog/2014/09/02/)
      2. [The quick and practical "MSI" hash table](https://nullprogram.com/blog/2022/08/08/)
  - [In Defense of Linked Lists on Ryan Fleury's Hidden Grove](https://www.rfleury.com/p/in-defense-of-linked-lists)
    - Ryan explains why linked lists are simple, flexible, and fast --
    especially in combination with arena allocators.
    - The RSS parsing code path produces a tree structure that also uses a
    linked list to store errors. It's based on the code Ryan provides in the
    section titled [The Power of Discontiguity](https://www.rfleury.com/i/75476949/the-power-of-discontiguity).
  - [Jeff Preshing's Blog](https://preshing.com/)
    - I read several posts from his blog to begin to develop my understanding of
    multithreaded applications and their synchronization using locks and atomic
    load and store primitives.

- Graphics and OpenGL
  - [microui](https://github.com/rxi/microui/)
    - I originally used microui as the UI library for this project until I
    wrote a custom one, but the one I built is heavily inspired by microui --
    even if it's much, much messier.
  - [Sol on Immediate Mode GUIs (IMGUI)](https://solhsa.com/imgui/index.html)
  - [docs.GL for OpenGL API Documentation](https://docs.gl/)

Then there are RFCs and specs for various formats/protocols and docs for all
the libraries I used, but those are easy to find.

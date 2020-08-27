# guilt
Graphs Used In Lovely Tasking

`guilt` is a C++20 coroutine-based tasking library that aims at helping to build task-based systems by providing built-in understanding of graph relationships.

Goals for version 0.1:

* be able to power a task-based system;
* provide dependency cycle detection and diagnostics, with debugging assistance;
* provide out-of-the-box profiling capabilities.

The primary projects that will use `guilt` initially are:
* [Vapor](github.com/reaver-project/vapor),
* a C++Now 2021 edition of what was supposed to be a C++Now 2020 talk by the author.

#!/bin/bash
find . -iname *.h -or -iname *.c | xargs clang-format -i

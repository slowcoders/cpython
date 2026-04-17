# Copilot instructions — CPython (rt-python repository)

This file helps future Copilot sessions and contributors understand how to build, test, and navigate this repository.

Build, test, and lint commands

- Configure and build (Unix/macOS/Cygwin):
  - ./configure [OPTIONS]
  - make -j$(nproc)
  - make install (or sudo make install)
- Out-of-tree build:
  - mkdir build && cd build && ../configure [OPTIONS] && make
- Run the test suite:
  - make test
- Re-run a single test or small set (examples):
  - make test TESTOPTS="-v test_os test_gdb"
  - make test TESTOPTS="-v test_module.TestClass.test_method"
  - After building a python binary: ./python -m test test_module.TestCase.test_name
- Linting / formatting (if available in dev environment):
  - ruff check .  # repo contains ruff config in places; install ruff to use
  - Use Tools/ and scripts/ in the repository for other checks (see Tools/)

High-level architecture

- Top-level layout (big picture):
  - Python/, Objects/, Include/: core interpreter implementation (C sources & headers).
  - Modules/: platform and extension modules implemented in C.
  - Lib/: pure-Python standard library modules and the stdlib test suite (Lib/test).
  - Parser/: PEG-based parser implementation and parser generator.
  - Programs/: executables and entry points (python main program, tools).
  - Tools/: developer tooling, build helpers, and scripts.
  - Doc/: Sphinx-based documentation source for the project.
- Build & test flow:
  - configure generates platform-specific Makefiles; make builds interpreter and tools; make test runs the integrated test harness that discovers tests in Lib/test and test/.

Key conventions and repository-specific patterns

- Tests:
  - Standard-library tests live under Lib/test or test/ and are run via the `make test` harness.
  - Use TESTOPTS to select specific tests and add -v for verbose output.
- C API and layout:
  - Public headers are under Include/ and sources split across Python/, Objects/, and Modules/.
  - Naming conventions: many exported C symbols begin with `Py`.
- Out-of-tree builds are supported and recommended for iterative development.
- Documentation edits belong in Doc/ (Sphinx). Use the Developer Guide for contributing detailed procedures: https://devguide.python.org/

AI assistant / other-config files

- No project-specific AI assistant config files (CLAUDE.md, .cursorrules, AGENTS.md, etc.) were found when this file was generated. If such files are added, include their important rules here.

Notes for Copilot sessions

- When asked to run tests, prefer using `make test TESTOPTS="..."` to scope runs; avoid full-suite runs unless requested.
- Prefer reading high-level files first: README.rst and Doc/README.rst, then inspect Python/, Lib/, Modules/, and Tools/ for implementation details.


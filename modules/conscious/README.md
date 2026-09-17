# Conscious (main and time sync)

- Multithread C executable.
- Will act as command input (suspend, end, resume...).
- Coordinate and be connected by all other modules.
- Start up, read config and, when all modules connected, run a simple state machine, receive control panel commands and stop everything when done.
- Synchronize the simulation steps of all modules.
- Will be a rudimentary control panel.
- Will log state changes if verbosity is enabled.

# Backlog of features

- In the future, receive orders from a remote control panel.

# References

- [Project documentation head](../../README.md)
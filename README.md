# bind-keys

Bind-keys provides a way to create shortcuts on headless linux machines.
To compile the program run `make`.
Bind-keys requires libconfig [[1]].

The shortcuts can be configured in `bind-keys.cfg`. This file also contains examples of the different options available.
The keys have to be specified using their keycodes.
To determine the keycodes bind-keys can be run with the `-s` flag to print the keycodes.

Modes allow assigning multiple functions to the same key combination by only executing shortcuts with the same or no mode.

[1]:https://github.com/hyperrealm/libconfig

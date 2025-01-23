This project is an application that bridges file-sharing clients operating in the IPv4 domain with clients in the IPv6 domain, and vice versa. It supports file search and transfer functionalities over dual-stack networks by acting as a gateway and proxy between the two protocols.

Features:

- Multicast File Search:
    - Supports IPv4 and IPv6 multicast groups for file search using UDP sockets.
    - Configurable multicast addresses and port for both groups.
- File Transfer Over TCP:
    - Facilitates file downloads and uploads using TCP sockets.
    - Manages connections and data transfer using independent threads for concurrency.
- Proxy Functionality:
    - Translates addresses between IPv4 and IPv6 for response packets.
    - Intermediates TCP connections to enable cross-protocol file transfers.
- Slow Mode for Testing:
    - Introduces a delay (0.5 seconds) between file transmission blocks to simulate slow connections or handle concurrent transfers.

How It Works:

1. Configuration:
    - The user sets up the multicast channel (IPv6 + IPv4 multicast addresses and a shared port).
2. Search & File Exchange:
    - File search requests are broadcast to the configured multicast groups.
    - Responses trigger threads for TCP connections to retrieve or send files.
3. Gateway Operation:
    - Acts as a proxy for file transfers between IPv4 and IPv6 clients by creating and managing threads for concurrent streams.
    - Maintains a list of active threads, received files, and transfer progress via the graphical interface.
4. Graphical Interface:
    - Displays shared files, active threads, and transfer status.
    - Allows users to modify the shared file list and configure application settings.

Code Overview:

This repository contains only the files authored by me as part of the project implementation:

- `callbacks.c`
- `callbacks.h`
- `proxy_thread.c`
- `proxy_thread.h`
- `callbacks_socket.c`
- `callbacks_socket.h`

To run the project, you will also need the following files provided by the course instructor: `sock.c`, `sock.h`, `gui.h`, `gui_g3.c`, `main.c`, and `Makefile`. These files are not included in this repository.

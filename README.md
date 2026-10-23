=========================================================

# Parking-Reservation
This project implements a distributed parking reservation system that addresses these challenges through a multi-server architecture. This system will feature basic user authentication, real-time availability checking, reservation processing, and dynamic pricing based on multiple factors including zone location, time of day, and duration.

=========================================================

I. What I Have Done in This Assignment

    I fully implemented a distributed parking reservation system using both TCP and UDP sockets.
    All functional requirements in the project specification have been completed, including:

    1. Main Server (Server M) handling all client interactions via TCP
    2. UDP communication with Server A (authentication), Server R (reservation), Server P (pricing/refund)

    3. Full support for:
        User authentication (member / guest)
        Searching parking availability
        Making reservations (including partial reservation Y/N interaction)
        Looking up existing reservations
        Canceling reservations and receiving refunds
    4. Concurrency using fork() allowing multiple clients to connect simultaneously
    5. All required on-screen messages exactly match the project specification
    6. Pricing server implements correct 2-hour slot billing + peak-hour multipliers
    7. Reservation state stored in memory and updated dynamically
    8. Completely robust TCP send/receive logic (send_all, recv_line)

=========================================================

II.  Code Files and Their Functions

    1. serverM.cpp (Main Server)
    Accepts TCP client connections
    Handles authentication, search, reserve, lookup, cancel
    Communicates with Server A, R, P via UDP
    Implements the required on-screen logs
    Implements Y/N interaction for partial reservations

    2. serverA.cpp (Authentication Server)
    Receives username/password via UDP
    Verifies credentials against member.txt and guest.txt
    Returns AUTH_OK_MEMBER, AUTH_OK_GUEST, or AUTH_FAIL

    3. serverR.cpp (Reservation Server)
    Loads parking space status from spaces.txt
    Manages all reservation logic:
        Availability lookup
        Full reservation
        Partial reservation with later confirmation (RESERVE_APPLY)
        Lookup
        Cancel
        Returns updated reservation status to Server M
        Ensures correct state transitions for partial cases

    4. serverP.cpp (Pricing Server)
    Receives usage information from Server M
    Calculates total cost based on:
        First 2 hours vs. additional hours
        Peak slot multipliers (slot 4 & 9)
        UPC vs. HSC pricing difference
    Calculates refund based on slot count

    5. client.cpp
    Handles the user interface
    Performs authentication
    Sends commands in lowercase format to Server M
    Displays main server responses
    Handles Y/N prompt for partial reservation

    6. Makefile
    Builds all servers and client

=========================================================

III. Message Formats Used in My Project

    1. TCP (Client ↔ Server M)
    Every message ends with newline \n

    Authentication:
        AUTH <username> <password>

    Commands (lowercase):
        search <lot>
        reserve <space> <slots...>
        lookup
        cancel <space> <slots...>
        QUIT

    2. UDP (Server M ↔ Server A/R/P)

    Authentication request:
        AUTH <username> <password>

    Response:
        AUTH_OK_MEMBER <username> <userid>
        AUTH_OK_GUEST guest
        AUTH_FAIL

    Reservation check:
        RESERVE_CHECK <space> <userid> <username> <slot1> <slot2> ...

    Partial reservation apply:
        RESERVE_APPLY <space> <userid> <username> <nReq> <slots...> <nBad> <badSlots...> <Y/N>

    Lookup:
        LOOKUP <userid>

    Cancel:
        CANCEL <space> <userid> <username> <slot1> ...

    Pricing:
        USAGE_PRICE <username>||<usage-block>

    Usage block example:
        U101 1,2,3
        H666 8

    Refund:
    USAGE_REFUND <username> <lot> <slotsCount*2hours>

=========================================================

    IV. Idiosyncrasies and Known Limitations

    1. The system keeps all reservation data in memory within Server R.
    Restarting server R resets all reservations.

    2. UDP has no guaranteed order; however, the project uses very small local messages so it is reliable in this context.

    3. Client must always enter lowercase commands as required by the project spec.

    4. Server M resolves partial reservations correctly, but if the client disconnects during Y/N prompt, the request is discarded.

    5. Pricing assumes each slot = 2 hours, exactly as defined in project.

=========================================================

    V. Reused Code

    I did not reuse code from external sources.

=========================================================

    VI. Ubuntu Version Used

    The project was completed and tested on:
    Ubuntu 24.04.3 LTS (provided USC EE450 VM)

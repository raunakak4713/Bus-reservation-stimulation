#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_ROWS 10
#define MAX_COLS 4
#define NAME_LEN 60
#define CITY_LEN 50
#define DATE_LEN 11
#define INITIAL_BOOKING_CAPACITY 16
#define INITIAL_BUS_CAPACITY 4
#define ADVANCE_DAYS 7
#define ADVANCE_DISCOUNT 0.10f

typedef struct {
    int id;
    char name[NAME_LEN];
    char source[CITY_LEN];
    char destination[CITY_LEN];
    char departureDate[DATE_LEN]; /* YYYY-MM-DD */
    char departureTime[8];        /* HH:MM */
    int rows;
    int cols;
    float baseFare;
    int seats[MAX_ROWS][MAX_COLS];
} Bus;

typedef struct {
    int bookingId;
    int busId;
    char passengerName[NAME_LEN];
    int seatRow;                  /* zero-based */
    int seatCol;                  /* zero-based */
    float fare;
    int active;
    char bookingDate[DATE_LEN];   /* YYYY-MM-DD */
} Booking;

typedef struct WaitNode {
    int busId;
    char passengerName[NAME_LEN];
    int seatsNeeded;
    char bookingDate[DATE_LEN];
    struct WaitNode *next;
} WaitNode;

typedef struct {
    WaitNode *front;
    WaitNode *rear;
    size_t count;
} WaitQueue;

static Bus *buses = NULL;
static size_t busCount = 0;
static size_t busCapacity = 0;

static Booking *bookings = NULL;
static size_t bookingCount = 0;
static size_t bookingCapacity = 0;

static WaitQueue waitlist = {NULL, NULL, 0};
static int nextBusId = 1001;
static int nextBookingId = 5001;

/* ---------- Safe input helpers ---------- */

static void readLine(const char *prompt, char *buffer, size_t size) {
    size_t len;
    int ch;

    if (prompt != NULL) {
        printf("%s", prompt);
        fflush(stdout);
    }

    if (fgets(buffer, (int)size, stdin) == NULL) {
        printf("\nInput ended. Exiting.\n");
        exit(EXIT_SUCCESS);
    }

    len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    } else {
        while ((ch = getchar()) != '\n' && ch != EOF) {
            /* Discard excess input. */
        }
    }

    /* Trim leading and trailing whitespace. */
    char *start = buffer;
    while (isspace((unsigned char)*start)) {
        start++;
    }
    if (start != buffer) {
        memmove(buffer, start, strlen(start) + 1);
    }
    len = strlen(buffer);
    while (len > 0 && isspace((unsigned char)buffer[len - 1])) {
        buffer[--len] = '\0';
    }
}

static int readInt(const char *prompt, int minValue, int maxValue) {
    char line[128];
    char *end;
    long value;

    for (;;) {
        readLine(prompt, line, sizeof(line));
        if (line[0] == '\0') {
            printf("Please enter a number.\n");
            continue;
        }

        value = strtol(line, &end, 10);
        while (isspace((unsigned char)*end)) {
            end++;
        }

        if (*end == '\0' && end != line &&
            value >= minValue && value <= maxValue) {
            return (int)value;
        }
        printf("Invalid input. Enter a value from %d to %d.\n",
               minValue, maxValue);
    }
}

static float readFloat(const char *prompt, float minValue, float maxValue) {
    char line[128];
    char *end;
    float value;

    for (;;) {
        readLine(prompt, line, sizeof(line));
        if (line[0] == '\0') {
            printf("Please enter an amount.\n");
            continue;
        }

        value = strtof(line, &end);
        while (isspace((unsigned char)*end)) {
            end++;
        }

        if (*end == '\0' && end != line &&
            value >= minValue && value <= maxValue) {
            return value;
        }
        printf("Invalid amount. Enter a value from %.2f to %.2f.\n",
               minValue, maxValue);
    }
}

static void readRequired(const char *prompt, char *buffer, size_t size) {
    do {
        readLine(prompt, buffer, size);
        if (buffer[0] == '\0') {
            printf("This field cannot be empty.\n");
        }
    } while (buffer[0] == '\0');
}

/* Checks a basic YYYY-MM-DD shape and numeric fields, not calendar validity. */
static int validDate(const char *date) {
    int i;
    if (strlen(date) != 10 || date[4] != '-' || date[7] != '-') {
        return 0;
    }
    for (i = 0; i < 10; i++) {
        if (i == 4 || i == 7) continue;
        if (!isdigit((unsigned char)date[i])) return 0;
    }
    return 1;
}

static void readDate(const char *prompt, char *date) {
    for (;;) {
        readRequired(prompt, date, DATE_LEN);
        if (validDate(date)) return;
        printf("Use YYYY-MM-DD format, for example 2026-11-15.\n");
    }
}

static void readTime(const char *prompt, char *timeText) {
    for (;;) {
        readRequired(prompt, timeText, 8);
        if (strlen(timeText) == 5 && timeText[2] == ':' &&
            isdigit((unsigned char)timeText[0]) &&
            isdigit((unsigned char)timeText[1]) &&
            isdigit((unsigned char)timeText[3]) &&
            isdigit((unsigned char)timeText[4])) {
            int hour = (timeText[0] - '0') * 10 + (timeText[1] - '0');
            int minute = (timeText[3] - '0') * 10 + (timeText[4] - '0');
            if (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59) {
                return;
            }
        }
        printf("Use a valid 24-hour time, for example 08:30 or 19:45.\n");
    }
}

/* ---------- Dynamic arrays ---------- */

static int ensureBusCapacity(void) {
    if (busCount < busCapacity) return 1;

    size_t newCapacity = (busCapacity == 0) ?
        INITIAL_BUS_CAPACITY : busCapacity * 2;
    Bus *newBuses = realloc(buses, newCapacity * sizeof(*buses));
    if (newBuses == NULL) {
        fprintf(stderr, "Error: could not allocate memory for buses.\n");
        return 0;
    }

    buses = newBuses;
    busCapacity = newCapacity;
    return 1;
}

static int ensureBookingCapacity(void) {
    if (bookingCount < bookingCapacity) return 1;

    size_t newCapacity = (bookingCapacity == 0) ?
        INITIAL_BOOKING_CAPACITY : bookingCapacity * 2;
    Booking *newBookings = realloc(bookings,
                                   newCapacity * sizeof(*bookings));
    if (newBookings == NULL) {
        fprintf(stderr, "Error: could not allocate memory for bookings.\n");
        return 0;
    }

    bookings = newBookings;
    bookingCapacity = newCapacity;
    return 1;
}

/* ---------- Lookups ---------- */

static Bus *findBus(int busId) {
    size_t i;
    for (i = 0; i < busCount; i++) {
        if (buses[i].id == busId) return &buses[i];
    }
    return NULL;
}

static Booking *findBooking(int bookingId) {
    size_t i;
    for (i = 0; i < bookingCount; i++) {
        if (bookings[i].bookingId == bookingId) return &bookings[i];
    }
    return NULL;
}

static int findBookingSeat(int busId, int row, int col, int bookingId) {
    Bus *bus = findBus(busId);
    if (bus == NULL || row < 0 || row >= bus->rows ||
        col < 0 || col >= bus->cols) return 0;
    return bus->seats[row][col] == bookingId;
}

static int busOccupiedSeats(const Bus *bus) {
    int occupied = 0;
    int r, c;
    for (r = 0; r < bus->rows; r++) {
        for (c = 0; c < bus->cols; c++) {
            if (bus->seats[r][c] != 0) occupied++;
        }
    }
    return occupied;
}

static int busTotalSeats(const Bus *bus) {
    return bus->rows * bus->cols;
}

/* ---------- Waitlist queue (linked list) ---------- */

static int enqueueWaitlist(int busId, const char *name, int seatsNeeded,
                           const char *bookingDate) {
    WaitNode *node = malloc(sizeof(*node));
    if (node == NULL) {
        printf("Unable to add this request to the waitlist (memory error).\n");
        return 0;
    }

    node->busId = busId;
    snprintf(node->passengerName, sizeof(node->passengerName), "%s", name);
    node->seatsNeeded = seatsNeeded;
    snprintf(node->bookingDate, sizeof(node->bookingDate), "%s", bookingDate);
    node->next = NULL;

    if (waitlist.rear == NULL) {
        waitlist.front = waitlist.rear = node;
    } else {
        waitlist.rear->next = node;
        waitlist.rear = node;
    }
    waitlist.count++;
    printf("Added to waitlist. Queue position: %zu\n", waitlist.count);
    return 1;
}

static void displayWaitlist(void) {
    WaitNode *node = waitlist.front;
    size_t position = 1;

    printf("\n================ WAITLIST ================\n");
    if (node == NULL) {
        printf("Waitlist is empty.\n");
        return;
    }

    printf("%-6s %-8s %-24s %-10s %-12s\n",
           "Pos.", "Bus ID", "Passenger", "Seats", "Booking date");
    while (node != NULL) {
        printf("%-6zu %-8d %-24s %-10d %-12s\n",
               position++, node->busId, node->passengerName,
               node->seatsNeeded, node->bookingDate);
        node = node->next;
    }
}

/* ---------- Seat type and fare ---------- */

/*
 * Four seats per row:
 * column 0 = window, column 1 = aisle, column 2 = aisle, column 3 = window.
 * An aisle premium applies to aisle seats; middle seats can be added if the
 * layout is changed. The four-seat layout here has no middle seat.
 */
static const char *seatType(int col) {
    if (col == 0 || col == 3) return "Window";
    return "Aisle";
}

static float calculateFare(const Bus *bus, int col,
                           const char *bookingDate) {
    float fare = bus->baseFare;
    int daysUntilDeparture = 0;

    /* Parse dates to compare them chronologically. */
    int by, bm, bd, ty, tm, td;
    if (sscanf(bookingDate, "%d-%d-%d", &by, &bm, &bd) == 3 &&
        sscanf(bus->departureDate, "%d-%d-%d", &ty, &tm, &td) == 3) {
        /*
         * Convert dates to a simple serial day count. This is a compact
         * Gregorian-date conversion for comparing dates in this simulation.
         */
        int y1 = by, m1 = bm, d1 = bd;
        int y2 = ty, m2 = tm, d2 = td;
        int a, y, m;
        a = (14 - m1) / 12; y = y1 + 4800 - a; m = m1 + 12 * a - 3;
        long day1 = d1 + (153L * m + 2) / 5 + 365L * y +
                    y / 4 - y / 100 + y / 400 - 32045;
        a = (14 - m2) / 12; y = y2 + 4800 - a; m = m2 + 12 * a - 3;
        long day2 = d2 + (153L * m + 2) / 5 + 365L * y +
                    y / 4 - y / 100 + y / 400 - 32045;
        daysUntilDeparture = (int)(day2 - day1);
    } else {
        daysUntilDeparture = 0;
    }

    if (col == 0 || col == 3) {
        fare += 50.0f; /* window premium */
    } else {
        fare += 20.0f; /* aisle premium */
    }

    if (daysUntilDeparture >= ADVANCE_DAYS) {
        fare *= (1.0f - ADVANCE_DISCOUNT);
    }
    return fare;
}

/* ---------- Display functions ---------- */

static void displayBuses(void) {
    size_t i;

    printf("\n================ BUS LIST ================\n");
    if (busCount == 0) {
        printf("No buses available.\n");
        return;
    }

    printf("%-8s %-20s %-16s %-16s %-12s %-8s %-10s %-10s\n",
           "Bus ID", "Bus name", "From", "To", "Date", "Time",
           "Seats", "Base fare");
    for (i = 0; i < busCount; i++) {
        printf("%-8d %-20s %-16s %-16s %-12s %-8s %-10d Rs.%-7.2f\n",
               buses[i].id, buses[i].name, buses[i].source,
               buses[i].destination, buses[i].departureDate,
               buses[i].departureTime, busTotalSeats(&buses[i]),
               buses[i].baseFare);
    }
}

static void displaySeatMap(const Bus *bus) {
    int r, c;

    printf("\nSeat map for Bus %d: %s (%s -> %s)\n",
           bus->id, bus->name, bus->source, bus->destination);
    printf("Driver/front is at the top. W=Window, A=Aisle\n\n");
    printf("       Left side       |       Right side\n");
    printf("       W      A        |        A      W\n");

    for (r = 0; r < bus->rows; r++) {
        printf("Row %2d ", r + 1);
        for (c = 0; c < bus->cols; c++) {
            int id = bus->seats[r][c];
            if (c == 2) printf(" | ");
            if (id == 0) {
                printf("[%2d:--] ", r * bus->cols + c + 1);
            } else {
                printf("[%2d:##] ", r * bus->cols + c + 1);
            }
        }
        printf("\n");
    }
    printf("\n[seat:--] available, [seat:##] booked\n");
}

static void displayBooking(const Booking *booking) {
    Bus *bus = findBus(booking->busId);
    printf("\n-------------- BOOKING DETAILS --------------\n");
    printf("Booking ID    : %d\n", booking->bookingId);
    printf("Passenger     : %s\n", booking->passengerName);
    printf("Bus ID        : %d\n", booking->busId);
    if (bus != NULL) {
        printf("Route         : %s -> %s\n", bus->source, bus->destination);
        printf("Departure     : %s %s\n",
               bus->departureDate, bus->departureTime);
    }
    printf("Seat          : Row %d, Column %d (%s)\n",
           booking->seatRow + 1, booking->seatCol + 1,
           seatType(booking->seatCol));
    printf("Fare          : Rs. %.2f\n", booking->fare);
    printf("Status        : %s\n", booking->active ? "ACTIVE" : "CANCELLED");
    printf("----------------------------------------------\n");
}

/* ---------- Bus management ---------- */

static void addBus(void) {
    Bus bus;
    int r, c;

    if (!ensureBusCapacity()) return;

    memset(&bus, 0, sizeof(bus));
    bus.id = nextBusId++;

    readRequired("Bus name: ", bus.name, sizeof(bus.name));
    readRequired("Source city: ", bus.source, sizeof(bus.source));
    readRequired("Destination city: ", bus.destination,
                 sizeof(bus.destination));
    readDate("Departure date (YYYY-MM-DD): ", bus.departureDate);
    readTime("Departure time (HH:MM, 24-hour): ", bus.departureTime);
    bus.rows = readInt("Number of seat rows (1-10): ", 1, MAX_ROWS);
    bus.cols = 4;
    bus.baseFare = readFloat("Base fare in rupees (1-100000): ",
                             1.0f, 100000.0f);

    for (r = 0; r < MAX_ROWS; r++) {
        for (c = 0; c < MAX_COLS; c++) bus.seats[r][c] = 0;
    }

    buses[busCount++] = bus;
    printf("Bus added successfully. Bus ID: %d\n", bus.id);
}

/* ---------- Booking creation ---------- */

static Booking *createBooking(Bus *bus, const char *name,
                              int row, int col, const char *bookingDate) {
    Booking booking;

    if (!ensureBookingCapacity()) return NULL;

    booking.bookingId = nextBookingId++;
    booking.busId = bus->id;
    snprintf(booking.passengerName, sizeof(booking.passengerName), "%s", name);
    booking.seatRow = row;
    booking.seatCol = col;
    booking.fare = calculateFare(bus, col, bookingDate);
    booking.active = 1;
    snprintf(booking.bookingDate, sizeof(booking.bookingDate), "%s",
             bookingDate);

    bookings[bookingCount] = booking;
    bus->seats[row][col] = booking.bookingId;
    bookingCount++;

    displayBooking(&bookings[bookingCount - 1]);
    return &bookings[bookingCount - 1];
}

static void bookSingleSeat(void) {
    int busId, row, col;
    char name[NAME_LEN];
    char bookingDate[DATE_LEN];
    Bus *bus;

    if (busCount == 0) {
        printf("No buses are registered. Add a bus first.\n");
        return;
    }

    displayBuses();
    busId = readInt("Enter bus ID: ", 1, 999999);
    bus = findBus(busId);
    if (bus == NULL) {
        printf("Bus not found.\n");
        return;
    }

    displaySeatMap(bus);
    readRequired("Passenger name: ", name, sizeof(name));
    readDate("Booking date (YYYY-MM-DD): ", bookingDate);

    if (busOccupiedSeats(bus) == busTotalSeats(bus)) {
        printf("This bus is full.\n");
        if (readInt("Join waitlist? (1=Yes, 0=No): ", 0, 1) == 1) {
            enqueueWaitlist(bus->id, name, 1, bookingDate);
        }
        return;
    }

    row = readInt("Seat row: ", 1, bus->rows) - 1;
    col = readInt("Seat column (1=left window, 2=left aisle, "
                  "3=right aisle, 4=right window): ", 1, bus->cols) - 1;

    if (bus->seats[row][col] != 0) {
        printf("That seat is already booked. Please try another seat.\n");
        return;
    }

    createBooking(bus, name, row, col, bookingDate);
}

static int findContiguousSeats(const Bus *bus, int groupSize,
                               int *foundRow, int *foundStartCol) {
    int r, c, consecutive;

    if (groupSize < 1 || groupSize > bus->cols) return 0;

    for (r = 0; r < bus->rows; r++) {
        consecutive = 0;
        for (c = 0; c < bus->cols; c++) {
            if (bus->seats[r][c] == 0) {
                consecutive++;
                if (consecutive == groupSize) {
                    *foundRow = r;
                    *foundStartCol = c - groupSize + 1;
                    return 1;
                }
            } else {
                consecutive = 0;
            }
        }
    }
    return 0;
}

static void bookGroupSeats(void) {
    int busId, groupSize, row, startCol, i;
    char name[NAME_LEN];
    char bookingDate[DATE_LEN];
    Bus *bus;

    if (busCount == 0) {
        printf("No buses are registered. Add a bus first.\n");
        return;
    }

    displayBuses();
    busId = readInt("Enter bus ID: ", 1, 999999);
    bus = findBus(busId);
    if (bus == NULL) {
        printf("Bus not found.\n");
        return;
    }

    displaySeatMap(bus);
    groupSize = readInt("Number of passengers in group (1-4): ", 1, 4);
    readRequired("Group contact / passenger name: ", name, sizeof(name));
    readDate("Booking date (YYYY-MM-DD): ", bookingDate);

    if (!findContiguousSeats(bus, groupSize, &row, &startCol)) {
        printf("No contiguous block of %d seats is available.\n", groupSize);
        if (readInt("Join waitlist for this group? (1=Yes, 0=No): ",
                    0, 1) == 1) {
            enqueueWaitlist(bus->id, name, groupSize, bookingDate);
        }
        return;
    }

    printf("Found contiguous seats in row %d, columns %d-%d.\n",
           row + 1, startCol + 1, startCol + groupSize);

    for (i = 0; i < groupSize; i++) {
        char passengerLabel[NAME_LEN];
        if (groupSize == 1) {
            snprintf(passengerLabel, sizeof(passengerLabel), "%s", name);
        } else {
            snprintf(passengerLabel, sizeof(passengerLabel), "%.34s (Group seat %d)",
                     name, i + 1);
        }

        if (createBooking(bus, passengerLabel, row, startCol + i,
                          bookingDate) == NULL) {
            /*
             * Allocation failure can occur mid-group. Seats already created
             * remain valid bookings; report partial success explicitly.
             */
            printf("Warning: memory allocation failed; group booking "
                   "completed only partially.\n");
            break;
        }
    }
}

/* ---------- Cancellation and waitlist processing ---------- */

static void processWaitlistForBus(int busId) {
    WaitNode *previous = NULL;
    WaitNode *current = waitlist.front;

    /*
     * Preserve FIFO order among requests for this bus. Requests for other
     * buses remain in their original positions. A request is fulfilled only
     * when its entire group can be seated contiguously.
     */
    while (current != NULL) {
        Bus *bus = findBus(current->busId);
        int row, startCol, i;
        WaitNode *next = current->next;

        if (current->busId == busId && bus != NULL &&
            findContiguousSeats(bus, current->seatsNeeded, &row, &startCol)) {
            char baseName[NAME_LEN];
            char bookingDate[DATE_LEN];
            int groupSize = current->seatsNeeded;

            snprintf(baseName, sizeof(baseName), "%s", current->passengerName);
            snprintf(bookingDate, sizeof(bookingDate), "%s",
                     current->bookingDate);

            for (i = 0; i < groupSize; i++) {
                char passengerLabel[NAME_LEN];
                if (groupSize == 1) {
                    snprintf(passengerLabel, sizeof(passengerLabel), "%s",
                             baseName);
                } else {
                    snprintf(passengerLabel, sizeof(passengerLabel),
                             "%.34s (Group seat %d)", baseName, i + 1);
                }
                if (createBooking(bus, passengerLabel, row, startCol + i,
                                  bookingDate) == NULL) {
                    printf("Waitlist request could not be completed due to "
                           "memory allocation failure.\n");
                    return;
                }
            }

            printf("Waitlist request for %s has been fulfilled.\n", baseName);

            if (previous == NULL) waitlist.front = next;
            else previous->next = next;

            if (waitlist.rear == current) waitlist.rear = previous;
            free(current);
            waitlist.count--;
            current = next;
        } else {
            previous = current;
            current = next;
        }
    }
}

static void cancelBooking(void) {
    int bookingId;
    Booking *booking;
    Bus *bus;

    bookingId = readInt("Enter booking ID to cancel: ", 1, 999999999);
    booking = findBooking(bookingId);

    if (booking == NULL || !booking->active) {
        printf("Active booking not found.\n");
        return;
    }

    bus = findBus(booking->busId);
    if (bus == NULL) {
        printf("Associated bus no longer exists.\n");
        return;
    }

    if (!findBookingSeat(booking->busId, booking->seatRow,
                         booking->seatCol, booking->bookingId)) {
        printf("Seat map and booking record are inconsistent; cancellation "
               "aborted for safety.\n");
        return;
    }

    bus->seats[booking->seatRow][booking->seatCol] = 0;
    booking->active = 0;
    printf("Booking %d cancelled. Seat released.\n", bookingId);
    processWaitlistForBus(bus->id);
}

/* ---------- Search and reports ---------- */

static int equalsIgnoreCase(const char *a, const char *b);

static void searchBooking(void) {
    int bookingId;
    Booking *booking;

    bookingId = readInt("Enter booking ID: ", 1, 999999999);
    booking = findBooking(bookingId);
    if (booking == NULL) {
        printf("Booking not found.\n");
        return;
    }
    displayBooking(booking);
}


static void displayAllBookings(void) {
    size_t i;
    int found = 0;

    printf("\n================ ALL BOOKINGS ================\n");
    printf("%-8s %-8s %-28s %-8s %-10s %-10s\n",
           "Book ID", "Bus ID", "Passenger", "Seat", "Fare", "Status");

    for (i = 0; i < bookingCount; i++) {
        const Booking *booking = &bookings[i];
        printf("%-8d %-8d %-28.28s R%-2dC%-2d   Rs.%-7.2f %-10s\n",
               booking->bookingId, booking->busId,
               booking->passengerName, booking->seatRow + 1,
               booking->seatCol + 1, booking->fare,
               booking->active ? "ACTIVE" : "CANCELLED");
        found = 1;
    }
    if (!found) printf("No bookings have been created yet.\n");
}

static void cancelWaitlistRequest(void) {
    int busId;
    char name[NAME_LEN];
    WaitNode *current = waitlist.front;
    WaitNode *previous = NULL;

    if (waitlist.front == NULL) {
        printf("Waitlist is empty.\n");
        return;
    }

    busId = readInt("Bus ID for the waitlist request: ", 1, 999999);
    readRequired("Passenger/group name exactly as displayed: ",
                 name, sizeof(name));

    while (current != NULL) {
        if (current->busId == busId &&
            equalsIgnoreCase(current->passengerName, name)) {
            if (previous == NULL) waitlist.front = current->next;
            else previous->next = current->next;

            if (waitlist.rear == current) waitlist.rear = previous;
            free(current);
            waitlist.count--;
            printf("Waitlist request removed.\n");
            return;
        }
        previous = current;
        current = current->next;
    }
    printf("No matching waitlist request was found.\n");
}

static void dailyRevenueReport(void) {
    char date[DATE_LEN];
    size_t i;
    float total = 0.0f;
    int count = 0;

    readDate("Report booking date (YYYY-MM-DD): ", date);
    printf("\nRevenue report for booking date %s\n", date);

    for (i = 0; i < bookingCount; i++) {
        if (bookings[i].active &&
            strcmp(bookings[i].bookingDate, date) == 0) {
            total += bookings[i].fare;
            count++;
        }
    }
    printf("Active bookings on date : %d\n", count);
    printf("Revenue on date         : Rs. %.2f\n", total);
}

static void revenueReport(void) {
    size_t i;
    float totalRevenue = 0.0f;
    float cancelledValue = 0.0f;

    printf("\n================ REVENUE REPORT ================\n");
    printf("Revenue is the sum of active booking fares in this simulation.\n");

    for (i = 0; i < bookingCount; i++) {
        if (bookings[i].active) totalRevenue += bookings[i].fare;
        else cancelledValue += bookings[i].fare;
    }

    printf("Active bookings       : ");
    {
        size_t activeCount = 0;
        for (i = 0; i < bookingCount; i++) {
            if (bookings[i].active) activeCount++;
        }
        printf("%zu\n", activeCount);
    }
    printf("Active booking revenue: Rs. %.2f\n", totalRevenue);
    printf("Cancelled fare value  : Rs. %.2f (excluded from revenue)\n",
           cancelledValue);
}

static void occupancyReport(void) {
    size_t i;
    int grandOccupied = 0;
    int grandCapacity = 0;

    printf("\n================ OCCUPANCY REPORT ================\n");
    if (busCount == 0) {
        printf("No buses registered.\n");
        return;
    }

    printf("%-8s %-20s %-12s %-12s %-12s\n",
           "Bus ID", "Bus name", "Occupied", "Capacity", "Occupancy");
    for (i = 0; i < busCount; i++) {
        int occupied = busOccupiedSeats(&buses[i]);
        int capacity = busTotalSeats(&buses[i]);
        float percentage = capacity > 0 ?
            (100.0f * occupied / capacity) : 0.0f;

        printf("%-8d %-20s %-12d %-12d %10.2f%%\n",
               buses[i].id, buses[i].name, occupied, capacity, percentage);
        grandOccupied += occupied;
        grandCapacity += capacity;
    }

    printf("TOTAL: %d / %d seats occupied (%.2f%%)\n",
           grandOccupied, grandCapacity,
           grandCapacity > 0 ? 100.0f * grandOccupied / grandCapacity : 0.0f);
}

static int equalsIgnoreCase(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void routeSearch(void) {
    char source[CITY_LEN];
    char destination[CITY_LEN];
    size_t i;
    int found = 0;

    readRequired("Enter source city: ", source, sizeof(source));
    readRequired("Enter destination city: ", destination, sizeof(destination));

    printf("\nRoutes matching %s -> %s:\n", source, destination);
    for (i = 0; i < busCount; i++) {
        if (equalsIgnoreCase(buses[i].source, source) &&
            equalsIgnoreCase(buses[i].destination, destination)) {
            printf("Bus %d | %s | %s %s | Base fare Rs. %.2f | "
                   "%d seats available\n",
                   buses[i].id, buses[i].name, buses[i].departureDate,
                   buses[i].departureTime, buses[i].baseFare,
                   busTotalSeats(&buses[i]) - busOccupiedSeats(&buses[i]));
            found = 1;
        }
    }
    if (!found) printf("No matching route found.\n");
}

/* ---------- Seed data and cleanup ---------- */

static void seedDemoBuses(void) {
    Bus demo;
    int r, c;

    memset(&demo, 0, sizeof(demo));
    demo.id = nextBusId++;
    snprintf(demo.name, sizeof(demo.name), "Intercity Express");
    snprintf(demo.source, sizeof(demo.source), "Delhi");
    snprintf(demo.destination, sizeof(demo.destination), "Chandigarh");
    snprintf(demo.departureDate, sizeof(demo.departureDate), "2026-11-15");
    snprintf(demo.departureTime, sizeof(demo.departureTime), "08:30");
    demo.rows = 8;
    demo.cols = 4;
    demo.baseFare = 800.0f;
    for (r = 0; r < MAX_ROWS; r++) {
        for (c = 0; c < MAX_COLS; c++) demo.seats[r][c] = 0;
    }

    if (ensureBusCapacity()) buses[busCount++] = demo;

    memset(&demo, 0, sizeof(demo));
    demo.id = nextBusId++;
    snprintf(demo.name, sizeof(demo.name), "CityLink Deluxe");
    snprintf(demo.source, sizeof(demo.source), "Kanpur");
    snprintf(demo.destination, sizeof(demo.destination), "Lucknow");
    snprintf(demo.departureDate, sizeof(demo.departureDate), "2026-11-10");
    snprintf(demo.departureTime, sizeof(demo.departureTime), "10:15");
    demo.rows = 6;
    demo.cols = 4;
    demo.baseFare = 450.0f;
    for (r = 0; r < MAX_ROWS; r++) {
        for (c = 0; c < MAX_COLS; c++) demo.seats[r][c] = 0;
    }

    if (ensureBusCapacity()) buses[busCount++] = demo;
}

static void cleanup(void) {
    WaitNode *node = waitlist.front;
    while (node != NULL) {
        WaitNode *next = node->next;
        free(node);
        node = next;
    }
    free(buses);
    free(bookings);
    buses = NULL;
    bookings = NULL;
    waitlist.front = waitlist.rear = NULL;
    waitlist.count = 0;
}

/* ---------- Main menu ---------- */

static void showMenu(void) {
    printf("\n====================================================\n");
    printf(" REAL-TIME BUS ROUTE & SEAT RESERVATION (SIMULATION)\n");
    printf("====================================================\n");
    printf("1. View all buses\n");
    printf("2. Search buses by route\n");
    printf("3. View seat map\n");
    printf("4. Book a single seat\n");
    printf("5. Book contiguous seats for a group\n");
    printf("6. Cancel booking\n");
    printf("7. Search booking by ID\n");
    printf("8. View waitlist\n");
    printf("9. Revenue report\n");
    printf("10. Occupancy report\n");
    printf("11. Add a bus\n");
    printf("12. List all bookings\n");
    printf("13. Cancel a waitlist request\n");
    printf("14. Daily revenue report\n");
    printf("0. Exit\n");
}

int main(void) {
    int choice;

    seedDemoBuses();
    printf("Demo buses loaded. Booking dates are entered manually for "
           "the pricing simulation.\n");

    do {
        showMenu();
        choice = readInt("Enter your choice: ", 0, 14);

        switch (choice) {
            case 1:
                displayBuses();
                break;
            case 2:
                routeSearch();
                break;
            case 3: {
                int busId;
                Bus *bus;
                displayBuses();
                busId = readInt("Enter bus ID: ", 1, 999999);
                bus = findBus(busId);
                if (bus == NULL) printf("Bus not found.\n");
                else displaySeatMap(bus);
                break;
            }
            case 4:
                bookSingleSeat();
                break;
            case 5:
                bookGroupSeats();
                break;
            case 6:
                cancelBooking();
                break;
            case 7:
                searchBooking();
                break;
            case 8:
                displayWaitlist();
                break;
            case 9:
                revenueReport();
                break;
            case 10:
                occupancyReport();
                break;
            case 11:
                addBus();
                break;
            case 12:
                displayAllBookings();
                break;
            case 13:
                cancelWaitlistRequest();
                break;
            case 14:
                dailyRevenueReport();
                break;
            case 0:
                printf("Exiting system. Releasing allocated memory...\n");
                break;
            default:
                printf("Invalid menu option.\n");
        }
    } while (choice != 0);

    cleanup();
    printf("Memory released. Goodbye!\n");
    return 0;
}

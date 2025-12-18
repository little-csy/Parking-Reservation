// client.cpp

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

using namespace std;

const int ID_SUFFIX = 148;
const char *SERVER_HOST = "127.0.0.1";
const int SERVERM_TCP_PORT = 25000 + ID_SUFFIX;
const int MAXBUFLEN = 4096;

void die(const string &msg) {
    perror(msg.c_str());
    exit(1);
}

void sendLine(int sockfd, const string &s) {
    string data = s + "\n";
    size_t total = 0;
    while (total < data.size()) {
        ssize_t n = send(sockfd, data.c_str() + total, data.size() - total, 0);
        if (n <= 0) die("send");
        total += n;
    }
}

bool recvLine(int sockfd, string &out) {
    out.clear();
    char c;
    while (true) {
        ssize_t n = recv(sockfd, &c, 1, 0);
        if (n == 0) return false;
        else if (n < 0) {
            if (errno == EINTR) continue;
            die("recv");
        }
        if (c == '\n') break;
        out.push_back(c);
    }
    return true;
}

/*
 * Reads lines until "---End---" or "---NeedYN---".
 * Returns:
 *   false → normal end
 *   true → server is waiting for Y/N input
 */
bool recvMultiLines(int sockfd) {
    string line;
    while (recvLine(sockfd, line)) {
        if (line == "---End---")
            return false;
        if (line == "---NeedYN---")
            return true;
        cout << line << endl;
    }
    return false;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        cerr << "Usage: ./client <username> <unencrypted_password>" << endl;
        return 1;
    }

    string username = argv[1];
    string password = argv[2];

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) die("socket");

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVERM_TCP_PORT);
    inet_pton(AF_INET, SERVER_HOST, &serv_addr.sin_addr);

    if (connect(sockfd, (sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        die("connect");
    }

    cout << "Client is up and running." << endl;

    // Send username/password
    cout << username << " sent an authentication request to the main server." << endl;
    ostringstream auth;
    auth << "AUTH " << username << " " << password;
    sendLine(sockfd, auth.str());

    string line;
    if (!recvLine(sockfd, line)) {
        cerr << "Authentication failed: connection closed." << endl;
        close(sockfd);
        return 1;
    }

    // Eval auth result
    if (line.rfind("AUTH_OK_MEMBER", 0) == 0) {
        cout << username << " received the authentication result." << endl;
        cout << "Authentication successful." << endl;
    }
    else if (line.rfind("AUTH_OK_GUEST", 0) == 0) {
        cout << "You have been granted guest access." << endl;
        username = "Guest";
    }
    else {
        cout << "Authentication failed: username or password is incorrect." << endl;
        close(sockfd);
        return 0;
    }

    // MAIN LOOP
    while (true) {
        cout << "> ";
        string cmd;
        if (!getline(cin, cmd)) break;
        if (cmd.empty()) continue;

        istringstream iss(cmd);
        string token;
        iss >> token;

        if (cmd == "help") {
            if (username == "Guest")
                cout << "Please enter the command: <search <parking lot>>, <quit>" << endl;
            else
                cout << "Please enter the command: <search <parking lot>>, <reserve <space>, <timeslots>>, <lookup>, <cancel <space> <timeslots>>, <quit>" << endl;
            continue;
        }

        if (cmd == "quit") {
            sendLine(sockfd, "QUIT");
            recvMultiLines(sockfd);
            close(sockfd);
            return 0;
        }

        // SEARCH
        if (token == "search") {
            if (username == "Guest")
                cout << "Guest sent an availability request to the main server." << endl;
            else
                cout << username << " sent an availability request to the main server." << endl;

            sendLine(sockfd, cmd);
            bool needYN = recvMultiLines(sockfd);
            if (needYN) {
                string yn;
                cout << "(Y/N)> ";
                getline(cin, yn);
                if (yn.empty()) yn = "N";
                sendLine(sockfd, yn);
                recvMultiLines(sockfd);
            }
            continue;
        }

        // RESERVE
        if (token == "reserve") {
            if (username == "Guest") {
                cout << "Guests can only check availability. Please log in as a member for full access." << endl;
                cout << "---Start a new request---" << endl;
                continue;
            }
            string space;
            if (!(iss >> space)) {
                cout << "Error: Space code and timeslot(s) are required. Please specify a space code and at least one timeslot." << endl;
                continue;
            }

            cout << username << " sent a reservation request to the main server." << endl;
            sendLine(sockfd, cmd);
            bool needYN = recvMultiLines(sockfd);
            if (needYN) {
                string yn;
                cout << "(Y/N)> ";
                getline(cin, yn);
                if (yn.empty()) yn = "N";
                sendLine(sockfd, yn);
                recvMultiLines(sockfd);
            }
            continue;
        }

        // LOOKUP
        if (token == "lookup") {
            if (username == "Guest") {
                cout << "Guests can only check availability. Please log in as a member for full access." << endl;
                cout << "---Start a new request---" << endl;
                continue;
            }
            cout << username << " sent a lookup request to the main server." << endl;
            sendLine(sockfd, cmd);
            bool needYN = recvMultiLines(sockfd);
            if (needYN) {
                string yn;
                cout << "(Y/N)> ";
                getline(cin, yn);
                if (yn.empty()) yn = "N";
                sendLine(sockfd, yn);
                recvMultiLines(sockfd);
            }
            continue;
        }

        // CANCEL
        if (token == "cancel") {
            if (username == "Guest") {
                cout << "Guests can only check availability. Please log in as a member for full access." << endl;
                cout << "---Start a new request---" << endl;
                continue;
            }
            string space;
            if (!(iss >> space)) {
                cout << "Error: Space code and timeslot(s) are required. Please specify a space code and at least one timeslot." << endl;
                continue;
            }
            cout << username << " sent a cancellation request to the main server." << endl;

            sendLine(sockfd, cmd);
            bool needYN = recvMultiLines(sockfd);
            if (needYN) {
                string yn;
                cout << "(Y/N)> ";
                getline(cin, yn);
                if (yn.empty()) yn = "N";
                sendLine(sockfd, yn);
                recvMultiLines(sockfd);
            }
            continue;
        }

        cout << "Unknown command. Type 'help' for usage." << endl;
    }

    close(sockfd);
    return 0;
}

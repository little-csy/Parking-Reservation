// serverM.cpp

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>

#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <csignal>

#define USC_ID_SUFFIX 148
#define SERVER_M_UDP_PORT (24000 + USC_ID_SUFFIX)
#define SERVER_M_TCP_PORT (25000 + USC_ID_SUFFIX)
#define SERVER_A_UDP_PORT (21000 + USC_ID_SUFFIX)
#define SERVER_R_UDP_PORT (22000 + USC_ID_SUFFIX)
#define SERVER_P_UDP_PORT (23000 + USC_ID_SUFFIX)
#define SERVER_HOST "127.0.0.1"

using namespace std;

int send_all_tcp(int sockfd, const string &msg) {
    const char *data = msg.c_str();
    size_t left = msg.size();
    while (left > 0) {
        ssize_t n = send(sockfd, data, left, 0);
        if (n <= 0) return -1;
        left -= n;
        data += n;
    }
    return 0;
}

int recv_line_tcp(int sockfd, string &out) {
    out.clear();
    char c;
    while (true) {
        ssize_t n = recv(sockfd, &c, 1, 0);
        if (n <= 0) return -1;
        if (c == '\n') break;
        out.push_back(c);
    }
    return 0;
}

// UDP request helper
string udp_request(int sockfd, int port, const string &msg) {
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, SERVER_HOST, &addr.sin_addr);

    socklen_t addr_len = sizeof(addr);
    if (sendto(sockfd, msg.c_str(), msg.size(), 0,
               (sockaddr *)&addr, addr_len) == -1) {
        perror("sendto");
        return "";
    }

    char buf[4096];
    ssize_t numbytes = recvfrom(sockfd, buf, sizeof(buf) - 1, 0,
                                (sockaddr *)&addr, &addr_len);
    if (numbytes <= 0) return "";
    buf[numbytes] = '\0';
    return string(buf);
}

// per-client handler
void handle_client(int new_fd, int udp_sock) {
    string line;

    // ================= AUTH phase =================
    if (recv_line_tcp(new_fd, line) == -1) {
        close(new_fd);
        return;
    }

    string cmd, username, password;
    {
        stringstream ss(line);
        ss >> cmd >> username >> password;
    }

    if (cmd != "AUTH") {
        close(new_fd);
        return;
    }

    cout << "Server M received username " << username << " and password ******." << endl;

    // send to Server A
    cout << "Server M sent the authentication request to Server A." << endl;
    string authReq = "AUTH " + username + " " + password + "\n";
    string authResp = udp_request(udp_sock, SERVER_A_UDP_PORT, authReq);

    cout << "Server M received the response from Server A using UDP over port "
         << SERVER_M_UDP_PORT << "." << endl;

    string status, respUser, userIdStr;
    {
        stringstream ss(authResp);
        ss >> status >> respUser >> userIdStr;
    }

    bool isGuest = false, isMember = false;
    int userId = 0;
    string toClient;

    if (status == "AUTH_OK_MEMBER") {
        isMember = true;
        userId = stoi(userIdStr);
        username = respUser;
        toClient = "AUTH_OK_MEMBER " + username + "\n";
    } else if (status == "AUTH_OK_GUEST") {
        isGuest = true;
        username = "Guest";
        toClient = "AUTH_OK_GUEST\n";
    } else {
        toClient = "AUTH_FAIL\n";
    }

    send_all_tcp(new_fd, toClient);
    cout << "Server M sent the response to the client using TCP over port "
         << SERVER_M_TCP_PORT << "." << endl;

    if (!isGuest && !isMember) {
        close(new_fd);
        return;
    }

    // ================= COMMAND LOOP =================
    while (true) {
        string req;
        if (recv_line_tcp(new_fd, req) == -1) {
            close(new_fd);
            return;
        }

        if (req == "QUIT") {
            close(new_fd);
            return;
        }

        string first;
        {
            stringstream ss(req);
            ss >> first;
        }

        // ================= SEARCH =================
        if (first == "search") {
            string lot;
            {
                stringstream ss(req);
                ss >> first >> lot;
            }

            cout << "Server M received an availability request from "
                 << (isGuest ? "Guest" : username)
                 << " for " << (lot.empty() ? "ALL" : lot)
                 << " using TCP over port " << SERVER_M_TCP_PORT << "." << endl;

            // send to R
            cout << "Server M sent the availability request to Server R." << endl;

            string rReq = lot.empty() ? "AVAIL\n" : ("AVAIL " + lot + "\n");
            string rResp = udp_request(udp_sock, SERVER_R_UDP_PORT, rReq);

            cout << "Server M received the response from Server R using UDP over port "
                 << SERVER_M_UDP_PORT << "." << endl;

            string out = "The client received the response from the main server using TCP over port "
                         + to_string(SERVER_M_TCP_PORT) + ".\n";

            stringstream ss(rResp);
            string flag;
            ss >> flag;

            if (flag == "INVALID_LOT") {
                out += "Invalid parking lot name.\n";
                out += "---Start a new request---\n";
            } else if (flag == "NO_SPACE") {
                string lname;
                ss >> lname;
                out += "No available spaces in " + lname + ".\n";
                out += "---Start a new request---\n";
            } else if (flag == "NO_SPACE_ALL") {
                out += "No available spaces in all parking lots.\n";
                out += "---Start a new request---\n";
            } else if (flag == "OK") {
                string remaining = rResp.substr(rResp.find('\n') + 1);
                string eachLine;
                stringstream ss2(remaining);
                while (getline(ss2, eachLine)) {
                    if (!eachLine.empty())
                        out += eachLine + "\n";
                }
                out += "---Start a new request---\n";
            } else {
                out += "Internal error in availability lookup.\n";
                out += "---Start a new request---\n";
            }

            send_all_tcp(new_fd, out);
            send_all_tcp(new_fd, "---End---\n");
            cout << "Server M sent the availability information to the client." << endl;
        }

        // ================= RESERVE =================
        else if (first == "reserve") {
            if (!isMember) {
                string out = "Guests can only check availability. Please log in as a member for full access.\n";
                out += "---Start a new request---\n";
                send_all_tcp(new_fd, out);
                send_all_tcp(new_fd, "---End---\n");
                continue;
            }

            string sid;
            vector<int> reqSlots;
            {
                stringstream ss(req);
                ss >> first >> sid;
                int t;
                while (ss >> t) reqSlots.push_back(t);
            }

            if (sid.empty() || reqSlots.empty()) {
                string out = "Error: Space code and timeslot(s) are required. Please specify a space code and at least one timeslot.\n";
                out += "---Start a new request---\n";
                send_all_tcp(new_fd, out);
                send_all_tcp(new_fd, "---End---\n");
                continue;
            }

            cout << "Server M received a reservation request from " << username
                 << " using TCP over port " << SERVER_M_TCP_PORT << "." << endl;

            // send reservation to R
            cout << "Server M sent the reservation request to Server R." << endl;

            // Use RESERVE_CHECK on Server R
            stringstream ssReq;
            ssReq << "RESERVE_CHECK " << sid << " " << userId << " " << username;
            for (int s : reqSlots) ssReq << " " << s;
            ssReq << "\n";

            string rResp = udp_request(udp_sock, SERVER_R_UDP_PORT, ssReq.str());

            cout << "Server M received the response from Server R using UDP over port "
                 << SERVER_M_UDP_PORT << "." << endl;

            stringstream ss(rResp);
            string rStatus;
            ss >> rStatus;

            string out = "The client received the response from the main server using TCP over port "
                         + to_string(SERVER_M_TCP_PORT) + ".\n";

            // FAIL: nothing reserved
            if (rStatus == "FAIL") {
                out += "Reservation failed. No slots were reserved.\n";
                out += "---Start a new request---\n";
                send_all_tcp(new_fd, out);
                send_all_tcp(new_fd, "---End---\n");
                cout << "Server M sent the reservation result to the client." << endl;
                continue;
            }

            // PARTIAL: need Y/N interaction
            if (rStatus == "PARTIAL") {
                vector<int> unavail;
                int x;
                while (ss >> x) unavail.push_back(x);

                out += "Time slot(s)";
                for (int u : unavail) out += " " + to_string(u);
                out += " are not available.\n";
                out += "Do you want to reserve the remaining slots? (Y/N)\n";
                out += "---NeedYN---\n";

                send_all_tcp(new_fd, out);
                cout << "Server M sent the partial reservation confirmation request to the client." << endl;

                // Wait for the Y/N answer from client
                string yn;
                if (recv_line_tcp(new_fd, yn) == -1) {
                    close(new_fd);
                    return;
                }

                // send confirmation to R
                stringstream applyReq;
                applyReq << "RESERVE_APPLY " << sid << " " << userId << " " << username << " ";

                // encode reqSlots
                applyReq << reqSlots.size() << " ";
                for (int s : reqSlots) applyReq << s << " ";

                // encode unavail
                applyReq << unavail.size() << " ";
                for (int u : unavail) applyReq << u << " ";

                applyReq << yn << "\n";

                string applyResp = udp_request(udp_sock, SERVER_R_UDP_PORT, applyReq.str());
                cout << "Server M sent the confirmation response to Server R." << endl;

                stringstream ssA(applyResp);
                string aStatus;
                ssA >> aStatus;

                string out2 = "The client received the response from the main server using TCP over port "
                              + to_string(SERVER_M_TCP_PORT) + ".\n";

                if (aStatus == "CANCELLED" || yn == "N" || yn == "n") {
                    out2 += "Reservation cancelled.\n";
                    out2 += "---Start a new request---\n";
                    send_all_tcp(new_fd, out2);
                    send_all_tcp(new_fd, "---End---\n");
                    cout << "Server M sent the reservation result to the client." << endl;
                    continue;
                }

                // OK Y case
                vector<int> finalReserved;
                int t2;
                while (ssA >> t2) finalReserved.push_back(t2);

                // Get updated usage for pricing
                string uReq = "USER_SLOTS " + to_string(userId) + "\n";
                string usage2 = udp_request(udp_sock, SERVER_R_UDP_PORT, uReq);

                cout << "Server M sent the pricing request to Server P." << endl;
                string pReq2 = "USAGE_PRICE " + username + "||" + usage2;
                string priceStr2 = udp_request(udp_sock, SERVER_P_UDP_PORT, pReq2);
                if (priceStr2.empty()) priceStr2 = "0.00";

                cout << "Server M received the pricing information from Server P using UDP over port "
                     << SERVER_M_UDP_PORT << "." << endl;

                out2 += "Reservation successful for " + sid + " at time slot(s)";
                for (int t : finalReserved) out2 += " " + to_string(t);
                out2 += ".\n";
                out2 += "Total cost: $" + priceStr2 + "\n";
                out2 += "---Start a new request---\n";

                send_all_tcp(new_fd, out2);
                send_all_tcp(new_fd, "---End---\n");
                cout << "Server M sent the reservation result to the client." << endl;
                continue;
            }

            // OK: all requested slots reserved successfully
            if (rStatus == "OK") {
                // Query all user slots for pricing
                string uReq = "USER_SLOTS " + to_string(userId) + "\n";
                string usage = udp_request(udp_sock, SERVER_R_UDP_PORT, uReq);

                cout << "Server M sent the pricing request to Server P." << endl;
                string pReq = "USAGE_PRICE " + username + "||" + usage;
                string priceStr = udp_request(udp_sock, SERVER_P_UDP_PORT, pReq);
                if (priceStr.empty()) priceStr = "0.00";

                cout << "Server M received the pricing information from Server P using UDP over port "
                     << SERVER_M_UDP_PORT << "." << endl;

                out += "Reservation successful for " + sid + " at time slot(s)";
                for (int s : reqSlots) out += " " + to_string(s);
                out += ".\n";
                out += "Total cost: $" + priceStr + "\n";
                out += "---Start a new request---\n";

                send_all_tcp(new_fd, out);
                send_all_tcp(new_fd, "---End---\n");
                cout << "Server M sent the reservation result to the client." << endl;
                continue;
            }

            // Unknown status
            out += "Reservation failed due to an internal error.\n";
            out += "---Start a new request---\n";
            send_all_tcp(new_fd, out);
            send_all_tcp(new_fd, "---End---\n");
            cout << "Server M sent the reservation result to the client." << endl;
        }

        // ================= LOOKUP =================
        else if (first == "lookup") {
            if (!isMember) {
                string out = "Guests can only check availability. Please log in as a member for full access.\n";
                out += "---Start a new request---\n";
                send_all_tcp(new_fd, out);
                send_all_tcp(new_fd, "---End---\n");
                continue;
            }

            cout << "Server M received a lookup request from " << username
                 << " using TCP over port " << SERVER_M_TCP_PORT << "." << endl;

            cout << "Server M sent the lookup request to Server R." << endl;
            string rReq = "LOOKUP " + to_string(userId) + "\n";
            string rResp = udp_request(udp_sock, SERVER_R_UDP_PORT, rReq);

            cout << "Server M received the response from Server R using UDP over port "
                 << SERVER_M_UDP_PORT << "." << endl;

            string out = "The client received the response from the main server using TCP over port "
                         + to_string(SERVER_M_TCP_PORT) + ".\n";

            if (rResp == "NONE") {
                out += "No reservations found.\n";
            } else {
                string flag;
                stringstream ss(rResp);
                ss >> flag;
                if (flag == "OK") {
                    string remaining = rResp.substr(rResp.find('\n') + 1);
                    string line2;
                    stringstream ss2(remaining);
                    while (getline(ss2, line2)) {
                        if (!line2.empty())
                            out += line2 + "\n";
                    }
                } else {
                    string line2;
                    stringstream ss2(rResp);
                    while (getline(ss2, line2)) {
                        if (!line2.empty())
                            out += line2 + "\n";
                    }
                }
            }

            out += "---Start a new request---\n";
            send_all_tcp(new_fd, out);
            send_all_tcp(new_fd, "---End---\n");
            cout << "Server M sent the lookup result to the client." << endl;
        }

        // ================= CANCEL =================
        else if (first == "cancel") {
            if (!isMember) {
                string out = "Guests cannot cancel reservations. Please log in as a member.\n";
                out += "---Start a new request---\n";
                send_all_tcp(new_fd, out);
                send_all_tcp(new_fd, "---End---\n");
                continue;
            }

            string sid;
            vector<int> reqSlots;
            {
                stringstream ss(req);
                ss >> first >> sid;
                int t;
                while (ss >> t) reqSlots.push_back(t);
            }

            if (sid.empty() || reqSlots.empty()) {
                string out = "The cancellation command must be followed by a space code and at least one timeslot.\n";
                out += "---Start a new request---\n";
                send_all_tcp(new_fd, out);
                send_all_tcp(new_fd, "---End---\n");
                continue;
            }

            cout << "Server M received a cancellation request from " << username
                 << " using TCP over port " << SERVER_M_TCP_PORT << "." << endl;

            cout << "Server M sent the cancellation request to Server R." << endl;

            stringstream ssReq;
            ssReq << "CANCEL " << sid << " " << userId << " " << username;
            for (int s : reqSlots) ssReq << " " << s;
            ssReq << "\n";

            string rResp = udp_request(udp_sock, SERVER_R_UDP_PORT, ssReq.str());

            cout << "Server M received the response from Server R using UDP over port "
                 << SERVER_M_UDP_PORT << "." << endl;

            stringstream ss(rResp);
            string cStatus;
            ss >> cStatus;

            string out = "The client received the response from the main server using TCP over port "
                         + to_string(SERVER_M_TCP_PORT) + ".\n";

            if (cStatus != "OK") {
                out += "Cancellation failed: You do not have reservations for the specified slots.\n";
                out += "---Start a new request---\n";
                send_all_tcp(new_fd, out);
                send_all_tcp(new_fd, "---End---\n");
                cout << "Server M sent the cancellation result to the client." << endl;
                continue;
            }

            vector<int> cancelled;
            int v;
            while (ss >> v) cancelled.push_back(v);

            // Compute hours = slots * 2
            int hours = (int)cancelled.size() * 2;
            string lotName = (sid[0] == 'U') ? "UPC" : "HSC";

            cout << "Server M sent the refund request to Server P." << endl;
            stringstream pReq;
            pReq << "USAGE_REFUND " << username << " " << lotName << " " << hours << "\n";
            string refundStr = udp_request(udp_sock, SERVER_P_UDP_PORT, pReq.str());
            if (refundStr.empty()) refundStr = "0.00";

            cout << "Server M received the refund information from Server P using UDP over port "
                 << SERVER_M_UDP_PORT << "." << endl;

            out += "Cancellation successful for " + sid + " at time slot(s)";
            for (int s : cancelled) out += " " + to_string(s);
            out += ".\n";
            out += "Refund amount: $" + refundStr + "\n";
            out += "---Start a new request---\n";

            send_all_tcp(new_fd, out);
            send_all_tcp(new_fd, "---End---\n");
            cout << "Server M sent the cancellation result to the client." << endl;
        }

        // ================= UNKNOWN =================
        else {
            string out = "Unknown command.\n";
            out += "---Start a new request---\n";
            send_all_tcp(new_fd, out);
            send_all_tcp(new_fd, "---End---\n");
        }
    }

    close(new_fd);
}

int main() {
    // Avoid zombie processes
    signal(SIGCHLD, SIG_IGN);

    // UDP socket
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock == -1) {
        perror("socket udp");
        return 1;
    }

    struct sockaddr_in my_udp_addr{};
    my_udp_addr.sin_family = AF_INET;
    my_udp_addr.sin_port = htons(SERVER_M_UDP_PORT);
    inet_pton(AF_INET, SERVER_HOST, &my_udp_addr.sin_addr);

    if (bind(udp_sock, (sockaddr *)&my_udp_addr, sizeof(my_udp_addr)) == -1) {
        perror("bind udp");
        close(udp_sock);
        return 1;
    }

    cout << "[Server M] Booting up using UDP on port " << SERVER_M_UDP_PORT << "." << endl;

    // TCP listening socket
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == -1) {
        perror("socket tcp");
        close(udp_sock);
        return 1;
    }

    int yes = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

    struct sockaddr_in my_tcp_addr{};
    my_tcp_addr.sin_family = AF_INET;
    my_tcp_addr.sin_port = htons(SERVER_M_TCP_PORT);
    inet_pton(AF_INET, SERVER_HOST, &my_tcp_addr.sin_addr);

    if (bind(listen_sock, (sockaddr *)&my_tcp_addr, sizeof(my_tcp_addr)) == -1) {
        perror("bind tcp");
        close(listen_sock);
        close(udp_sock);
        return 1;
    }

    if (listen(listen_sock, 10) == -1) {
        perror("listen");
        close(listen_sock);
        close(udp_sock);
        return 1;
    }

    cout << "[Server M] Booting up TCP on port " << SERVER_M_TCP_PORT << " to serve clients." << endl;

    // Accept loop with fork() for concurrency
    while (true) {
        struct sockaddr_in client_addr{};
        socklen_t sin_size = sizeof(client_addr);
        int new_fd = accept(listen_sock, (sockaddr *)&client_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(new_fd);
            continue;
        }

        if (pid == 0) {
            // Child process
            close(listen_sock);
            handle_client(new_fd, udp_sock);
            close(udp_sock);
            exit(0);
        } else {
            // Parent
            close(new_fd);
        }
    }

    close(listen_sock);
    close(udp_sock);
    return 0;
}

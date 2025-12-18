// serverA.cpp

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

using namespace std;

const int ID_SUFFIX = 148;
const int SERVERA_UDP_PORT = 21000 + ID_SUFFIX;
const char *LOCAL_HOST = "127.0.0.1";
const int MAXBUFLEN = 4096;

// username -> (userID, encryptedPassword)
map<string, pair<int, string>> users;

void die(const string &msg) {
    perror(msg.c_str());
    exit(1);
}

// As required by the project: letters/digits are shifted by +3 (cyclic), special characters remain unchanged
string encryptPassword(const string &plain) {
    string out = plain;
    for (char &c : out) {
        if (c >= 'a' && c <= 'z') {
            c = 'a' + (c - 'a' + 3) % 26;
        } else if (c >= 'A' && c <= 'Z') {
            c = 'A' + (c - 'A' + 3) % 26;
        } else if (c >= '0' && c <= '9') {
            c = '0' + (c - '0' + 3) % 10;
        }
    }
    return out;
}

void loadMembers() {
    ifstream fin("members.txt");
    if (!fin) {
        cerr << "Cannot open members.txt" << endl;
        return;
    }
    string line;
    while (getline(fin, line)) {
        if (line.empty()) continue;
        istringstream iss(line);
        int uid;
        string uname, enc;
        if (iss >> uid >> uname >> enc) {
            users[uname] = {uid, enc};
        }
    }
}

int main() {
    loadMembers();

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) die("socket");

    sockaddr_in my_addr{};
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(SERVERA_UDP_PORT);
    inet_pton(AF_INET, LOCAL_HOST, &my_addr.sin_addr);
    memset(my_addr.sin_zero, 0, sizeof(my_addr.sin_zero));

    if (bind(sockfd, (sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
        die("bind");
    }

    cout << "[Server A] Booting up using UDP on port " << SERVERA_UDP_PORT << "." << endl;

    char buf[MAXBUFLEN];

    while (true) {
        sockaddr_in their_addr{};
        socklen_t addr_len = sizeof(their_addr);
        ssize_t numbytes = recvfrom(sockfd, buf, MAXBUFLEN - 1, 0,
                                    (sockaddr *)&their_addr, &addr_len);
        if (numbytes < 0) {
            if (errno == EINTR) continue;
            die("recvfrom");
        }
        buf[numbytes] = '\0';
        string msg(buf);

        istringstream iss(msg);
        string cmd, username, password;
        iss >> cmd >> username >> password;

        if (cmd != "AUTH") {
            continue;
        }

        cout << "[Server A] Received username " << username << " and password ******." << endl;

        string reply;

        // guest
        if (username == "guest" && password == "123456") {
            cout << "[Server A] Guest has been authenticated." << endl;
            reply = "AUTH_OK_GUEST\n";
        } else {
            auto it = users.find(username);
            string enc = encryptPassword(password);

            if (it != users.end() && it->second.second == enc) {
                cout << "[Server A] Member " << username << " has been authenticated." << endl;
                reply = "AUTH_OK_MEMBER " + username + " " + to_string(it->second.first) + "\n";
            } else {
                cout << "[Server A] The username " << username
                     << " or password ****** is incorrect." << endl;
                reply = "AUTH_FAIL\n";
            }
        }

        if (sendto(sockfd, reply.c_str(), reply.size(), 0,
                   (sockaddr *)&their_addr, addr_len) == -1) {
            die("sendto");
        }
    }

    close(sockfd);
    return 0;
}

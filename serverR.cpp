// serverR.cpp

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
#include <vector>

using namespace std;

const int ID_SUFFIX = 148;
const int SERVERR_UDP_PORT = 22000 + ID_SUFFIX;
const char *LOCAL_HOST = "127.0.0.1";
const int MAXBUFLEN = 4096;

// spaceID -> vector<int> size 13 (1..12)
map<string, vector<int>> spaces;

void die(const string &msg) {
    perror(msg.c_str());
    exit(1);
}

void loadSpaces() {
    ifstream fin("spaces.txt");
    if (!fin) {
        cerr << "Cannot open spaces.txt" << endl;
        return;
    }
    string line;
    while (getline(fin, line)) {
        if (line.empty()) continue;
        istringstream iss(line);
        string sid;
        iss >> sid;
        vector<int> slots(13, 0);
        for (int i = 1; i <= 12; ++i) {
            int v;
            if (!(iss >> v)) v = 0;
            slots[i] = v;
        }
        spaces[sid] = slots;
    }
}

// Construct the availability list for a specific parking lot
string handleAvailability(const string &lotName) {
    // lotName: "UPC" or "HSC", or an empty string (indicating all lots)
    bool allLots = lotName.empty();
    char lotPrefix = 0;
    if (!allLots) {
        if (lotName == "UPC") lotPrefix = 'U';
        else if (lotName == "HSC") lotPrefix = 'H';
        else return "INVALID_LOT";
    }

    ostringstream oss;
    bool any = false;
    for (auto &p : spaces) {
        const string &sid = p.first;
        if (!allLots) {
            if (sid[0] != lotPrefix) continue;
        }
        vector<int> &slots = p.second;
        vector<int> freeSlots;
        for (int i = 1; i <= 12; ++i) {
            if (slots[i] == 0) freeSlots.push_back(i);
        }
        if (!freeSlots.empty()) {
            any = true;
            oss << sid << ":";
            for (size_t i = 0; i < freeSlots.size(); ++i) {
                oss << " " << freeSlots[i];
            }
            oss << "\n";
        }
    }
    if (!any) {
        if (!allLots)
            return string("NO_SPACE ") + lotName;
        else
            return "NO_SPACE_ALL";
    }
    return "OK\n" + oss.str();
}

// Member reservation: returns a string describing the result
// Input: spaceID, vector<int> slots, userID
string handleReserve(const string &sid, const vector<int> &reqSlots,
                     int userID, bool &allAvail,
                     vector<int> &unavail, vector<int> &reserved)
{
    auto it = spaces.find(sid);
    if (it == spaces.end()) {
        allAvail = false;
        unavail = reqSlots;
        return "FAIL_NO_SPACE";
    }

    vector<int> &slots = it->second;
    allAvail = true;
    unavail.clear();

    // find unavailable slots
    for (int s : reqSlots) {
        if (s < 1 || s > 12 || slots[s] != 0) {
            allAvail = false;
            unavail.push_back(s);
        }
    }

    // Case 1: all available
    if (allAvail) {
        reserved = reqSlots;
        for (int s : reqSlots) slots[s] = userID;
        return "OK";
    }

    // Case 2: all requested slots are unavailable → direct FAIL (NO Y/N!!)
    if ((int)unavail.size() == (int)reqSlots.size()) {
        return "FAIL";
    }

    // Case 3: some available, some unavailable → PARTIAL
    return "PARTIAL";
}

void applyReservePartial(const string &sid, const vector<int> &reqSlots,
                         const vector<int> &unavail, int userID,
                         vector<int> &reservedSlots) {
    vector<bool> isBad(13, false);
    for (int s : unavail) {
        if (s >= 1 && s <= 12)
            isBad[s] = true;
    }
    auto it = spaces.find(sid);
    if (it == spaces.end()) return;
    vector<int> &slots = it->second;
    for (int s : reqSlots) {
        if (s < 1 || s > 12) continue;
        if (!isBad[s] && slots[s] == 0) {
            slots[s] = userID;
            reservedSlots.push_back(s);
        }
    }
}

// lookup: return all current reservations for this user
string handleLookup(int userID) {
    ostringstream oss;
    bool any = false;
    for (auto &p : spaces) {
        const string &sid = p.first;
        const vector<int> &slots = p.second;
        vector<int> mine;
        for (int i = 1; i <= 12; ++i) {
            if (slots[i] == userID) mine.push_back(i);
        }
        if (!mine.empty()) {
            any = true;
            oss << sid << ":";
            for (int s : mine) oss << " " << s;
            oss << "\n";
        }
    }
    if (!any) return "NONE";
    return "OK\n" + oss.str();
}

// cancel: given spaceID + slots, cancel them if they belong to this user
string handleCancel(const string &sid, const vector<int> &reqSlots,
                    int userID, vector<int> &canceledSlots) {
    auto it = spaces.find(sid);
    if (it == spaces.end()) return "FAIL_NO_SPACE";
    vector<int> &slots = it->second;
    for (int s : reqSlots) {
        if (s < 1 || s > 12 || slots[s] != userID) {
            return "FAIL";
        }
    }
    for (int s : reqSlots) {
        slots[s] = 0;
        canceledSlots.push_back(s);
    }
    return "OK";
}

// Get all occupied slots for a specific user across all parking lots, used by the pricing server
string buildUserAllSlots(int userID) {
    // Format: if none, return "NONE"
    // If reservations exist: each space on one line, e.g. "sid 1,2,3"
    bool any = false;
    ostringstream oss;
    for (auto &p : spaces) {
        const string &sid = p.first;
        const vector<int> &slots = p.second;
        vector<int> mine;
        for (int i = 1; i <= 12; ++i) {
            if (slots[i] == userID) mine.push_back(i);
        }
        if (!mine.empty()) {
            any = true;
            oss << sid << " ";
            for (size_t i = 0; i < mine.size(); ++i) {
                if (i) oss << ",";
                oss << mine[i];
            }
            oss << "\n";
        }
    }
    if (!any) return "NONE";
    return oss.str();
}

int main() {
    loadSpaces();

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) die("socket");

    sockaddr_in my_addr{};
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(SERVERR_UDP_PORT);
    inet_pton(AF_INET, LOCAL_HOST, &my_addr.sin_addr);

    if (bind(sockfd, (sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
        die("bind");
    }

    cout << "Server R is up and running using UDP on port " << SERVERR_UDP_PORT
         << "." << endl;

    char buf[MAXBUFLEN];

    while (true) {
        sockaddr_in their_addr{};
        socklen_t addr_len = sizeof(their_addr);
        ssize_t numbytes =
            recvfrom(sockfd, buf, MAXBUFLEN - 1, 0, (sockaddr *)&their_addr,
                     &addr_len);
        if (numbytes < 0) {
            if (errno == EINTR) continue;
            die("recvfrom");
        }
        buf[numbytes] = '\0';
        string msg(buf);
        istringstream iss(msg);
        string cmd;
        iss >> cmd;

        if (cmd == "AVAIL") {
            string lot;
            iss >> lot;
            cout << "Server R received an availability request from the main server."
                 << endl;
            string resp = handleAvailability(lot);
            if (resp.rfind("OK", 0) == 0) {
                cout << "Server R finished sending the response to the main server."
                     << endl;
            }
            sendto(sockfd, resp.c_str(), resp.size(), 0, (sockaddr *)&their_addr,
                   addr_len);
        } else if (cmd == "RESERVE_CHECK") {
            cout << "Server R received a reservation request from the main server."
                 << endl;
            string sid, username;
            int userID;
            iss >> sid >> userID >> username;

            vector<int> reqSlots;
            int s;
            while (iss >> s) reqSlots.push_back(s);

            bool allAvail;
            vector<int> unavail, reserved;
            string status = handleReserve(sid, reqSlots, userID, allAvail,
                                          unavail, reserved);

            ostringstream resp;
            if (status == "OK") {
                cout << "All requested time slots are available." << endl;
                cout << "Successfully reserved " << sid << " at time slot(s)";
                for (size_t i = 0; i < reserved.size(); ++i) {
                    cout << " " << reserved[i];
                }
                cout << " for " << username << "." << endl;
                resp << "OK";
            } else if (status == "PARTIAL") {
                cout << "Time slot(s)";
                for (size_t i = 0; i < unavail.size(); ++i) {
                    cout << " " << unavail[i];
                }
                cout << " not available for " << sid << "." << endl;
                cout << "Requesting to reserve rest available slots (Y/N)."
                     << endl;
                resp << "PARTIAL";
                for (int x : unavail) resp << " " << x;
            } else {
                resp << "FAIL";
            }
            string r = resp.str();
            sendto(sockfd, r.c_str(), r.size(), 0, (sockaddr *)&their_addr,
                   addr_len);
        } else if (cmd == "RESERVE_APPLY") {
            string sid;
            int userID;
            string username;
            iss >> sid >> userID >> username;
            vector<int> reqSlots, unavail;
            int v;

            int nReq;
            iss >> nReq;
            for (int i = 0; i < nReq; ++i) {
                iss >> v;
                reqSlots.push_back(v);
            }
            int nBad;
            iss >> nBad;
            for (int i = 0; i < nBad; ++i) {
                iss >> v;
                unavail.push_back(v);
            }
            string yn;
            iss >> yn;

            vector<int> reservedSlots;
            if (yn == "Y" || yn == "y") {
                cout << "User confirmed partial reservation." << endl;
                applyReservePartial(sid, reqSlots, unavail, userID,
                                    reservedSlots);
                if (!reservedSlots.empty()) {
                    cout << "Successfully reserved " << sid << " at time slot(s)";
                    for (size_t i = 0; i < reservedSlots.size(); ++i) {
                        cout << " " << reservedSlots[i];
                    }
                    cout << " for " << username << "." << endl;
                }
                ostringstream resp;
                resp << "OK";
                for (int x : reservedSlots) resp << " " << x;
                string r = resp.str();
                sendto(sockfd, r.c_str(), r.size(), 0,
                       (sockaddr *)&their_addr, addr_len);
            } else {
                cout << "Reservation cancelled." << endl;
                string resp = "CANCELLED";
                sendto(sockfd, resp.c_str(), resp.size(), 0,
                       (sockaddr *)&their_addr, addr_len);
            }
        } else if (cmd == "LOOKUP") {
            cout << "Server R received a lookup request from the main server."
                 << endl;
            int userID;
            iss >> userID;
            string resp = handleLookup(userID);
            cout << "Server R finished sending the reservation information to the main server."
                 << endl;
            sendto(sockfd, resp.c_str(), resp.size(), 0, (sockaddr *)&their_addr,
                   addr_len);
        } else if (cmd == "CANCEL") {
            cout << "Server R received a cancellation request from the main server."
                 << endl;
            string sid;
            int userID;
            string username;
            iss >> sid >> userID >> username;
            vector<int> reqSlots, canceled;
            int s;
            while (iss >> s) reqSlots.push_back(s);
            string status = handleCancel(sid, reqSlots, userID, canceled);
            ostringstream resp;
            if (status == "OK") {
                cout << "Successfully cancelled reservation for " << sid
                     << " at time slot(s)";
                for (size_t i = 0; i < canceled.size(); ++i) {
                    cout << " " << canceled[i];
                }
                cout << " for " << username << "." << endl;
                resp << "OK";
                for (int x : canceled) resp << " " << x;
            } else {
                cout << "No reservation found for " << username << " at " << sid
                     << " time slot(s)";
                for (int x : reqSlots) cout << " " << x;
                cout << "." << endl;
                resp << "FAIL";
            }
            string r = resp.str();
            sendto(sockfd, r.c_str(), r.size(), 0, (sockaddr *)&their_addr,
                   addr_len);
        } else if (cmd == "USER_SLOTS") {
            int userID;
            iss >> userID;
            string result = buildUserAllSlots(userID);
            sendto(sockfd, result.c_str(), result.size(), 0,
                   (sockaddr *)&their_addr, addr_len);
        }
    }

    close(sockfd);
    return 0;
}

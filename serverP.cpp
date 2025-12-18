// serverP.cpp

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

using namespace std;

const int ID_SUFFIX = 148;
const int SERVERP_UDP_PORT = 23000 + ID_SUFFIX;
const char *LOCAL_HOST = "127.0.0.1";
const int MAXBUFLEN = 4096;

// Peak slots = 8–10AM & 4–6PM → slot #4 & #9
bool isPeak(int s) {
    return (s == 4 || s == 9);
}

/*
Each slot = 2 hours.
Hour 1 uses baseFirst (first 2 cumulative hours)
Hour 2 uses baseAfter (after 2 hours)
Peak slots multiply BOTH hours ×1.5
*/

// compute total lot price
double computeLotTotal(vector<int> slots, bool isUPC) {
    if (slots.empty()) return 0.0;

    sort(slots.begin(), slots.end());

    double total = 0.0;
    int usedHours = 0;

    double baseFirst = isUPC ? 10.0 : 15.0;
    double baseAfter = isUPC ? 7.0 : 10.0;

    for (int slot : slots) {
        bool peak = isPeak(slot);

        // slot = 2 hours → hour1 + hour2
        for (int h = 0; h < 2; h++) {
            double rate = (usedHours < 2 ? baseFirst : baseAfter);
            if (peak) rate *= 1.5;

            total += rate;
            usedHours++;
        }
    }
    return total;
}

// refund calculation
// Hours already passed from main server
double computeRefund(int hours, bool isUPC) {
    double perHour = isUPC ? 7.0 : 10.0;
    return perHour * hours;
}

// Usage data structure
struct LotUsage {
    vector<int> upc;
    vector<int> hsc;
};

// Parse usage returned by serverR
LotUsage parseUsage(const string &body) {
    LotUsage U;
    if (body == "NONE") return U;

    stringstream ss(body);
    string line;

    while (getline(ss, line)) {
        if (line.empty()) continue;

        string sid, list;
        stringstream ls(line);
        ls >> sid >> list;

        vector<int> slots;
        string num;
        stringstream s2(list);

        while (getline(s2, num, ',')) {
            if (!num.empty()) slots.push_back(stoi(num));
        }

        if (sid[0] == 'U') U.upc.insert(U.upc.end(), slots.begin(), slots.end());
        else if (sid[0] == 'H') U.hsc.insert(U.hsc.end(), slots.begin(), slots.end());
    }

    return U;
}

int main() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    sockaddr_in my{};
    my.sin_family = AF_INET;
    my.sin_port = htons(SERVERP_UDP_PORT);
    inet_pton(AF_INET, LOCAL_HOST, &my.sin_addr);

    bind(sock, (sockaddr *)&my, sizeof(my));

    cout << "Server P is up and running using UDP on port "
         << SERVERP_UDP_PORT << "." << endl;

    char buf[MAXBUFLEN];

    while (1) {
        sockaddr_in their{};
        socklen_t alen = sizeof(their);

        ssize_t n = recvfrom(sock, buf, MAXBUFLEN - 1, 0,
                             (sockaddr *)&their, &alen);

        if (n <= 0) continue;
        buf[n] = '\0';

        string msg(buf);
        stringstream ss(msg);
        string cmd;
        ss >> cmd;

        // PRICE
        if (cmd == "USAGE_PRICE") {
            string rest = msg.substr(strlen("USAGE_PRICE "));

            size_t pos = rest.find("||");
            string username = rest.substr(0, pos);
            string usage = (pos == string::npos ? "" : rest.substr(pos + 2));

            cout << "Server P received a pricing request from the main server." << endl;

            LotUsage U = parseUsage(usage);

            double totalUPC = computeLotTotal(U.upc, true);
            double totalHSC = computeLotTotal(U.hsc, false);
            double total = totalUPC + totalHSC;

            cout.setf(ios::fixed); 
            cout.precision(2);
            cout << "Calculated total price of $" << total
                 << " for " << username << "." << endl;

            ostringstream resp;
            resp.setf(ios::fixed);
            resp.precision(2);
            resp << total;

            sendto(sock, resp.str().c_str(), resp.str().size(), 0,
                   (sockaddr *)&their, alen);

            cout << "Server P finished sending the price to the main server." << endl;
        }

        // REFUND
        else if (cmd == "USAGE_REFUND") {
            string username, lotName;
            int hours;
            ss >> username >> lotName >> hours;

            cout << "Server P received a refund request from the main server." << endl;

            bool isUPC = (lotName == "UPC");
            double refund = computeRefund(hours, isUPC);

            cout.setf(ios::fixed); 
            cout.precision(2);
            cout << "Calculated refund of $" << refund
                 << " for " << username << "." << endl;

            ostringstream resp;
            resp.setf(ios::fixed);
            resp.precision(2);
            resp << refund;

            sendto(sock, resp.str().c_str(), resp.str().size(), 0,
                   (sockaddr *)&their, alen);

            cout << "Server P finished sending the refund amount to the main server." << endl;
        }
    }

    close(sock);
    return 0;
}

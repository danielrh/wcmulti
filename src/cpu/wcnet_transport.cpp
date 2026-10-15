#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <chrono>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <deque>
#include <vector>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include "wcnet_transport.h"
#include "wcnet_perf.h"
#include "wcnet_log.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace wc {

MessageCategory category_of(const NetworkMessage &msg) {
    return msg.has_chat() || msg.has_in_flight_scene() ? CAT_CHAT : CAT_GAME;
}

const char *message_type_name(const NetworkMessage &msg) {
    if (msg.has_connect()) return "connect";
    if (msg.has_game()) return "game";
    if (msg.has_frame()) return "frame";
    if (msg.has_chat()) return "chat";
    if (msg.has_start_mission_req()) return "start_mission_req";
    if (msg.has_start_briefing_req()) return "start_briefing_req";
    if (msg.has_briefing_start()) return "briefing_start";
    return "none";
}

// ---------------------------------------------------------------------------
// Connection: protobuf (de)serialization and per-category queues

Connection::Connection() : stream_(NULL) {}
Connection::~Connection() { close(); }

void Connection::adopt(Stream *stream) {
    close();
    stream_ = stream;
}

void Connection::close() {
    for (int i = 0; i < NUM_CATEGORIES; i++) {
        queues_[i].clear();
    }
    held_.clear();
    heldBestEffort_.clear();
    heldAfter_ = 0;
    heldFailed_ = false;
    if (stream_) {
        stream_->close();
        delete stream_;
        stream_ = NULL;
    }
}

bool Connection::send(const NetworkMessage &msg) {
    if (!stream_) {
        return false;
    }
    if (wclog_level() >= 3) {
        wclog(3, "SEND %s", msg.DebugString().c_str());
    }
    std::string out;
    if (!msg.SerializeToString(&out)) {
        wclog(0, "protobuf serialize failed");
        return false;
    }
    if (out.empty()) {
        wclog(0, "refusing to send an empty message");
        return false;
    }
    return stream_->send(out);
}

// Test aids: a slow or a bad connection, made on one machine (from the
// environment, so the page's ?env.NAME=value sets them too).  Each applies
// to what comes in, so with the same figures on both machines the round trip
// is twice the delay.
//   WCNET_LAG=<ms>      one-way delay: every message is held back that long
//   WCNET_JITTER=<ms>   on top of that, a random delay of up to that much,
//                       different for every message.  The link is ordered:
//                       a message cannot overtake the one before it, so a
//                       late one holds the next ones back too
//   WCNET_DROP=<pct>[:<ms>]  that share of the messages is lost in transit.
//                       The link is reliable: a lost message is sent again,
//                       and it and everything behind it arrive <ms> later
//                       (default: a round trip plus 150 ms, what the fast
//                       retransmit of SCTP or TCP costs once the other side
//                       has noticed the gap from the three messages that
//                       follow it at twenty a second)
//   WCNET_BURST=<periodMs>:<lossMs>[:<spikeMs>]  every period, the messages
//                       of <lossMs> are lost and those of the <spikeMs> after
//                       them are held up that long, a queue draining: what a
//                       satellite handover does (Starlink's, every 15 s).  By
//                       the wall clock, so that both machines have it at the
//                       same moments, as a dish does to both directions
//   WCNET_SEED=<n>      the random sequence (fixed by default: a run repeats)
struct Impairment {
    bool on;
    double lag, jitter, drop, recover;
    double burstPeriod, burstLoss, burstSpike;
    uint32_t state;  // xorshift
    Impairment() : on(false), lag(0), jitter(0), drop(0), recover(0), burstPeriod(0), burstLoss(0), burstSpike(0), state(0x9e3779b9u) {
        const char *env = getenv("WCNET_LAG");
        lag = env && env[0] ? atof(env) : 0;
        env = getenv("WCNET_JITTER");
        jitter = env && env[0] ? atof(env) : 0;
        env = getenv("WCNET_DROP");
        drop = env && env[0] ? atof(env) / 100.0 : 0;
        const char *colon = env ? strchr(env, ':') : NULL;
        recover = colon ? atof(colon + 1) : 2 * lag + 150;
        env = getenv("WCNET_BURST");
        if (env && env[0]) {
            burstPeriod = atof(env);
            const char *p = strchr(env, ':');
            burstLoss = p ? atof(p + 1) : 0;
            p = p ? strchr(p + 1, ':') : NULL;
            burstSpike = p ? atof(p + 1) : 0;
        }
        env = getenv("WCNET_SEED");
        if (env && env[0]) {
            state ^= (uint32_t)strtoul(env, NULL, 10) * 2654435761u;
        }
        if (lag < 0) lag = 0;
        if (jitter < 0) jitter = 0;
        if (drop < 0) drop = 0;
        if (drop > 1) drop = 1;
        if (burstPeriod <= 0 || burstLoss + burstSpike <= 0) burstPeriod = 0;
        on = lag > 0 || jitter > 0 || drop > 0 || burstPeriod > 0;
        if (on) {
            wclog(1, "simulated link: %.0f ms delay, %.0f ms jitter, %.1f%% of the messages lost (%.0f ms to recover one)%s",
                  lag, jitter, 100 * drop, recover, burstPeriod > 0 ? ", and bursts" : "");
            if (burstPeriod > 0) {
                wclog(1, "simulated link: every %.0f ms a burst: %.0f ms of loss, then %.0f ms of queue", burstPeriod, burstLoss, burstSpike);
            }
        }
    }
    // Where in the burst's period the wall clock is.
    double burst_phase() {
        if (burstPeriod <= 0) {
            return -1;
        }
        using namespace std::chrono;
        double wall = duration<double, std::milli>(system_clock::now().time_since_epoch()).count();
        return fmod(wall, burstPeriod);
    }
    double random() {  // 0 <= r < 1
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (state >> 8) / 16777216.0;
    }
    // When a best-effort message that came in now is let through: -1 when it
    // is lost (nothing sends it again), else now plus the delay and the
    // jitter, in no particular order.
    double due_best_effort(double now) {
        double phase = burst_phase();
        if ((drop > 0 && random() < drop) || (phase >= 0 && phase < burstLoss)) {
            return -1;
        }
        return now + lag + jitter * random() + (phase >= burstLoss && phase < burstLoss + burstSpike ? burstSpike : 0);
    }
    // When a message that came in now is let through.  (A burst of lost
    // messages costs one recovery plus their spacing: each is sent again
    // on its own account, and the first holds the others back.)
    double due(double now, double *after) {
        double at = now + lag + jitter * random();
        double phase = burst_phase();
        if (phase >= burstLoss && phase < burstLoss + burstSpike) {
            at += burstSpike;
        }
        if ((drop > 0 && random() < drop) || (phase >= 0 && phase < burstLoss)) {
            at += recover;
        }
        if (at < *after) {
            at = *after;  // behind the one before it
        }
        *after = at;
        return at;
    }
};

static Impairment &impairment() {
    static Impairment imp;
    return imp;
}

static void pause_ms(double ms) {
#ifdef __EMSCRIPTEN__
    emscripten_sleep((unsigned)(ms < 1 ? 1 : ms));
#else
    usleep((useconds_t)(ms * 1000.0));
#endif
}

RecvStatus Connection::read_raw(std::string &data, bool blocking) {
    Impairment &imp = impairment();
    if (!imp.on) {
        return stream_->recv(data, blocking);
    }
    // Take in everything that has arrived, each with its time to be let
    // through, then hand out what is due.
    while (!heldFailed_) {
        std::string in;
        RecvStatus st = stream_->recv(in, false);
        if (st.ok()) {
            held_.push_back(std::make_pair(imp.due(perf_now_ms(), &heldAfter_), in));
        } else {
            heldFailed_ = st.failed();
            break;
        }
    }
    if (held_.empty()) {
        if (heldFailed_) {
            return RecvStatus::STATUS_FAIL;
        }
        if (!blocking) {
            return RecvStatus::STATUS_NO_DATA;
        }
        std::string in;
        RecvStatus st = stream_->recv(in, true);
        if (!st.ok()) {
            return st;
        }
        held_.push_back(std::make_pair(imp.due(perf_now_ms(), &heldAfter_), in));
    }
    double wait = held_.front().first - perf_now_ms();
    if (wait > 0) {
        if (!blocking) {
            return RecvStatus::STATUS_NO_DATA;
        }
        pause_ms(wait);
    }
    data = held_.front().second;
    held_.pop_front();
    return RecvStatus::STATUS_OK;
}

RecvStatus Connection::read_raw_best_effort(std::string &data) {
    Impairment &imp = impairment();
    if (!imp.on) {
        return stream_->recv_best_effort(data);
    }
    for (;;) {
        std::string in;
        RecvStatus st = stream_->recv_best_effort(in);
        if (!st.ok()) {
            if (st.failed() && heldBestEffort_.empty()) {
                return st;
            }
            break;
        }
        double at = imp.due_best_effort(perf_now_ms());
        if (at < 0) {
            continue;  // lost
        }
        // In order of their times: a later one may overtake an earlier one.
        std::deque<std::pair<double, std::string> >::iterator it = heldBestEffort_.end();
        while (it != heldBestEffort_.begin() && (it - 1)->first > at) {
            --it;
        }
        heldBestEffort_.insert(it, std::make_pair(at, in));
    }
    if (heldBestEffort_.empty() || heldBestEffort_.front().first > perf_now_ms()) {
        return RecvStatus::STATUS_NO_DATA;
    }
    data = heldBestEffort_.front().second;
    heldBestEffort_.pop_front();
    return RecvStatus::STATUS_OK;
}

bool Connection::send_best_effort(const NetworkMessage &msg) {
    if (!stream_) {
        return false;
    }
    std::string out;
    if (!msg.SerializeToString(&out) || out.empty()) {
        return false;
    }
    return stream_->send_best_effort(out);
}

RecvStatus Connection::poll_best_effort(NetworkMessage &msg) {
    if (!stream_) {
        return RecvStatus::STATUS_FAIL;
    }
    std::string data;
    RecvStatus st = read_raw_best_effort(data);
    if (!st.ok()) {
        return st;
    }
    if (!msg.ParseFromArray(data.data(), (int)data.size())) {
        wclog(0, "protobuf parse failed (best effort)");
        return RecvStatus::STATUS_NO_DATA;
    }
    return RecvStatus::STATUS_OK;
}

RecvStatus Connection::read_one(NetworkMessage &msg, bool blocking) {
    if (!stream_) {
        return RecvStatus::STATUS_FAIL;
    }
    std::string data;
    RecvStatus st = read_raw(data, blocking);
    if (!st.ok()) {
        return st;
    }
    if (!msg.ParseFromArray(data.data(), (int)data.size())) {
        wclog(0, "protobuf parse failed");
        return RecvStatus::STATUS_FAIL;
    }
    if (wclog_level() >= 3) {
        wclog(3, "RECV %s", msg.DebugString().c_str());
    }
    return RecvStatus::STATUS_OK;
}

RecvStatus Connection::recv(MessageCategory cat, NetworkMessage &msg) {
    if (!queues_[cat].empty()) {
        msg = queues_[cat].front();
        queues_[cat].pop_front();
        return RecvStatus::STATUS_OK;
    }
    // Blocking: the time this takes is what a frame waits for the others.
    double began = perf_now_ms();
    while (true) {
        RecvStatus st = read_one(msg, true);
        if (!st.ok()) {
            return st;
        }
        MessageCategory got = category_of(msg);
        if (got == cat) {
            perf_wait(perf_now_ms() - began);
            return RecvStatus::STATUS_OK;
        }
        queues_[got].push_back(msg);
    }
}

RecvStatus Connection::poll(MessageCategory cat, NetworkMessage &msg) {
    if (!queues_[cat].empty()) {
        msg = queues_[cat].front();
        queues_[cat].pop_front();
        return RecvStatus::STATUS_OK;
    }
    // Everything that has arrived is read, until one of `cat` turns up: a
    // chat line in front of a frame must not hide the frame.
    while (true) {
        RecvStatus st = read_one(msg, false);
        if (!st.ok()) {
            return st;
        }
        MessageCategory got = category_of(msg);
        if (got == cat) {
            return RecvStatus::STATUS_OK;
        }
        queues_[got].push_back(msg);
    }
}

const NetworkMessage *Connection::peek(MessageCategory cat) const {
    if (queues_[cat].empty()) {
        return NULL;
    }
    return &queues_[cat].front();
}

// ---------------------------------------------------------------------------
// TCP: 3-byte big-endian length prefix per message; its top bit marks a
// best-effort message, which TCP carries reliably all the same (a test
// link's drops are the simulated ones, wcnet_transport.cpp above).

template <class Fn>
static ssize_t xfer_all(const Fn &fn, unsigned char *buf, size_t size) {
    size_t done = 0;
    while (size) {
        ssize_t cur = fn(buf, size);
        if (cur < 0) {
            if (errno == EINTR) {
                continue;
            }
            return cur;
        }
        if (cur == 0) {
            return (ssize_t)done;
        }
        done += cur;
        buf += cur;
        size -= cur;
    }
    return (ssize_t)done;
}

struct SendFn {
    int fd;
    ssize_t operator()(unsigned char *b, size_t n) const { return ::send(fd, b, n, 0); }
};
struct RecvFn {
    int fd;
    ssize_t operator()(unsigned char *b, size_t n) const { return ::recv(fd, b, n, 0); }
};

static std::string peer_name(int fd) {
    sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    char host[NI_MAXHOST], port[NI_MAXSERV];
    if (getpeername(fd, (sockaddr *)&addr, &len) == 0 &&
        getnameinfo((sockaddr *)&addr, len, host, sizeof(host), port, sizeof(port),
                    NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
        return std::string("tcp ") + host + ":" + port;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "tcp fd %d", fd);
    return buf;
}

class TcpStream : public Stream {
public:
    explicit TcpStream(int fd) : fd_(fd), name_(peer_name(fd)) {}
    ~TcpStream() { close(); }

    virtual bool is_open() const { return fd_ != -1; }

    virtual void close() {
        if (fd_ != -1) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    virtual bool send(const std::string &bytes) { return send_marked(bytes, false); }
    virtual bool send_best_effort(const std::string &bytes) { return send_marked(bytes, true); }

    virtual RecvStatus recv(std::string &bytes, bool blocking) {
        if (!reliable_.empty()) {
            bytes = reliable_.front();
            reliable_.pop_front();
            return RecvStatus::STATUS_OK;
        }
        for (;;) {
            bool bestEffort;
            RecvStatus st = read_frame(bytes, blocking, &bestEffort);
            if (!st.ok() || !bestEffort) {
                return st;
            }
            bestEffort_.push_back(bytes);
        }
    }

    virtual RecvStatus recv_best_effort(std::string &bytes) {
        while (bestEffort_.empty()) {
            bool bestEffort;
            RecvStatus st = read_frame(bytes, false, &bestEffort);
            if (!st.ok()) {
                return st;
            }
            if (bestEffort) {
                return RecvStatus::STATUS_OK;
            }
            reliable_.push_back(bytes);
        }
        bytes = bestEffort_.front();
        bestEffort_.pop_front();
        return RecvStatus::STATUS_OK;
    }

    virtual std::string describe() const { return name_; }

private:
    bool send_marked(const std::string &bytes, bool bestEffort) {
        if (fd_ == -1) {
            return false;
        }
        size_t len = bytes.length();
        if (len == 0 || len >= (1u << 23)) {
            wclog(0, "message size %d out of range for tcp framing", (int)len);
            return false;
        }
        std::string out;
        out.reserve(len + 3);
        out.push_back((char)((len >> 16) | (bestEffort ? 0x80 : 0)));
        out.push_back((char)(len >> 8));
        out.push_back((char)len);
        out += bytes;
        SendFn fn = { fd_ };
        if (xfer_all(fn, (unsigned char *)&out[0], out.length()) < (ssize_t)out.length()) {
            perror("wcnet send failed");
            return false;
        }
        return true;
    }

    // One message off the socket, of either kind.
    RecvStatus read_frame(std::string &bytes, bool blocking, bool *bestEffort) {
        if (fd_ == -1) {
            return RecvStatus::STATUS_FAIL;
        }
        if (!blocking && !readable()) {
            return RecvStatus::STATUS_NO_DATA;
        }
        unsigned char lengthData[3];
        RecvFn fn = { fd_ };
        ssize_t ret = xfer_all(fn, lengthData, sizeof(lengthData));
        if (ret < (ssize_t)sizeof(lengthData)) {
            if (ret < 0) {
                perror("wcnet recv length failed");
            } else {
                wclog(1, "connection closed by peer");
            }
            return RecvStatus::STATUS_FAIL;
        }
        *bestEffort = (lengthData[0] & 0x80) != 0;
        size_t len = ((size_t)(lengthData[0] & 0x7f) << 16) | ((size_t)lengthData[1] << 8) | lengthData[2];
        if (len == 0) {
            wclog(0, "empty message received");
            return RecvStatus::STATUS_FAIL;
        }
        bytes.resize(len);
        ret = xfer_all(fn, (unsigned char *)&bytes[0], len);
        if (ret < (ssize_t)len) {
            if (ret < 0) {
                perror("wcnet recv data failed");
            } else {
                wclog(1, "connection closed by peer mid-message");
            }
            return RecvStatus::STATUS_FAIL;
        }
        return RecvStatus::STATUS_OK;
    }

    bool readable() const {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd_, &set);
        struct timeval tv = { 0, 0 };
        return select(fd_ + 1, &set, NULL, NULL, &tv) > 0;
    }

    int fd_;
    std::string name_;
    std::deque<std::string> reliable_, bestEffort_;  // read while looking for the other kind
};

class TcpListener : public Listener {
public:
    TcpListener(int fd, const std::string &portstr) : fd_(fd), portstr_(portstr) {}
    ~TcpListener() { ::close(fd_); }

    virtual Stream *accept(bool blocking) {
        int flags = fcntl(fd_, F_GETFL, 0);
        fcntl(fd_, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK));
        sockaddr_storage addr;
        socklen_t addrlen = sizeof(addr);
        int s = ::accept(fd_, (sockaddr *)&addr, &addrlen);
        fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);
        if (s < 0) {
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                perror("wcnet accept failed");
            }
            return NULL;
        }
        return new TcpStream(s);
    }

    virtual std::string describe() const { return "tcp port " + portstr_; }

private:
    int fd_;
    std::string portstr_;
};

Listener *tcp_listen(const char *portstr) {
    struct addrinfo hints, *res0 = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    int err = getaddrinfo(NULL, portstr, &hints, &res0);
    if (err) {
        wclog(0, "getaddrinfo: %s", gai_strerror(err));
        return NULL;
    }
    int s = -1;
    for (struct addrinfo *res = res0; res; res = res->ai_next) {
        s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (s < 0) {
            continue;
        }
        int enable = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
        if (bind(s, res->ai_addr, res->ai_addrlen) < 0 || listen(s, 5) < 0) {
            perror("wcnet bind/listen");
            ::close(s);
            s = -1;
            continue;
        }
        break;
    }
    freeaddrinfo(res0);
    if (s < 0) {
        return NULL;
    }
    return new TcpListener(s, portstr);
}

Stream *tcp_connect(const char *host, const char *portstr) {
    struct addrinfo hints, *res0 = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int err = getaddrinfo(host, portstr, &hints, &res0);
    if (err) {
        wclog(0, "getaddrinfo %s: %s", host, gai_strerror(err));
        return NULL;
    }
    int s = -1;
    for (struct addrinfo *res = res0; res; res = res->ai_next) {
        s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (s < 0) {
            continue;
        }
        if (connect(s, res->ai_addr, res->ai_addrlen) < 0) {
            ::close(s);
            s = -1;
            continue;
        }
        break;
    }
    freeaddrinfo(res0);
    if (s < 0) {
        return NULL;
    }
    return new TcpStream(s);
}

}  // namespace wc

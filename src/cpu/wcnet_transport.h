/*
 *  Transport for the Wing Commander multiplayer protocol.
 *
 *  A Stream carries whole protobuf NetworkMessage blobs, reliably and in
 *  order, to one peer.  Two implementations exist:
 *    - TCP (this file): 3-byte big-endian length prefix per message;
 *    - lobbylink (wcnet_lobby.h): one WebRTC reliable DataChannel message
 *      per blob, peers found by room code instead of IP address.
 *  A Listener hands out Streams for incoming connections (server side).
 *
 *  A Connection wraps a Stream and keeps one queue per message category
 *  so that a blocking read of a game frame can park chat messages (and
 *  vice versa) instead of losing them.
 */
#ifndef WCNET_TRANSPORT_H_
#define WCNET_TRANSPORT_H_

#include <deque>
#include <string>
#include "wc.pb.h"

namespace wc {

class RecvStatus {
public:
    enum Type { STATUS_OK, STATUS_FAIL, STATUS_NO_DATA };
    RecvStatus(Type t = STATUS_FAIL) : t_(t) {}
    bool ok() const { return t_ == STATUS_OK; }
    bool no_data() const { return t_ == STATUS_NO_DATA; }
    bool failed() const { return t_ == STATUS_FAIL; }

private:
    Type t_;
};

enum MessageCategory {
    CAT_GAME = 0,   // frames, handshakes, mission state
    CAT_CHAT = 1,   // chat and the host's scene notices; never blocks
    NUM_CATEGORIES
};

MessageCategory category_of(const NetworkMessage &msg);
const char *message_type_name(const NetworkMessage &msg);

// A reliable, ordered pipe of whole messages to one peer, with a
// best-effort side channel: a datagram that may be lost or overtake
// another and never holds the reliable messages back, for what the next
// one supersedes (positions).  Over TCP it goes in the same stream, marked.
class Stream {
public:
    virtual ~Stream() {}
    virtual bool is_open() const = 0;
    // Tells the peer (its reads fail) and releases the resources.
    virtual void close() = 0;
    virtual bool send(const std::string &bytes) = 0;
    // Next whole message.  Non-blocking calls return NO_DATA when nothing is
    // pending; FAIL means the peer is gone.
    virtual RecvStatus recv(std::string &bytes, bool blocking) = 0;
    // (A transport without the channel sends it reliably, and has none to
    // receive.)
    virtual bool send_best_effort(const std::string &bytes) { return send(bytes); }
    // Next best-effort message, never waiting (NO_DATA: none).
    virtual RecvStatus recv_best_effort(std::string &bytes) {
        (void)bytes;
        return RecvStatus::STATUS_NO_DATA;
    }
    // For log lines, e.g. "tcp 10.0.0.2:13255" or "lobby player 1".
    virtual std::string describe() const = 0;
};

// Source of incoming connections.
class Listener {
public:
    virtual ~Listener() {}
    // Next incoming connection (caller owns it), or NULL when none is
    // pending (non-blocking) or on error.
    virtual Stream *accept(bool blocking) = 0;
    virtual std::string describe() const = 0;
};

class Connection {
public:
    Connection();
    ~Connection();

    // Takes ownership; closes any previous stream first.
    void adopt(Stream *stream);
    void close();
    bool is_open() const { return stream_ != NULL && stream_->is_open(); }
    std::string describe() const { return stream_ ? stream_->describe() : "not connected"; }

    bool send(const NetworkMessage &msg);
    // The best-effort channel (Stream): a message that may be lost.
    bool send_best_effort(const NetworkMessage &msg);
    // Non-blocking: the next best-effort message, NO_DATA when none has arrived.
    RecvStatus poll_best_effort(NetworkMessage &msg);

    // Blocking receive of the next message of `cat`.  Messages of other
    // categories that arrive meanwhile are queued.
    RecvStatus recv(MessageCategory cat, NetworkMessage &msg);
    // Non-blocking: returns NO_DATA when nothing of `cat` has arrived
    // (messages of other categories that have are queued).
    RecvStatus poll(MessageCategory cat, NetworkMessage &msg);
    // Look at the next queued message of `cat` without consuming it.
    const NetworkMessage *peek(MessageCategory cat) const;
    size_t queued(MessageCategory cat) const { return queues_[cat].size(); }

private:
    RecvStatus read_one(NetworkMessage &msg, bool blocking);
    RecvStatus read_raw(std::string &data, bool blocking);
    RecvStatus read_raw_best_effort(std::string &data);

    Stream *stream_;
    // Test aid (a simulated link, wcnet_transport.cpp): what came in, held
    // back until its time, and the time of the last one (none overtakes it);
    // the best-effort messages apart (one may overtake another, a lost one
    // is lost).
    std::deque<std::pair<double, std::string> > held_;
    std::deque<std::pair<double, std::string> > heldBestEffort_;
    double heldAfter_ = 0;
    bool heldFailed_ = false;
    std::deque<NetworkMessage> queues_[NUM_CATEGORIES];
};

// TCP.  Both return NULL on failure (logged).
Listener *tcp_listen(const char *portstr);
Stream *tcp_connect(const char *host, const char *portstr);

}  // namespace wc

#endif

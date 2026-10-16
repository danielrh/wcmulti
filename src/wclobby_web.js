/*
 *  wclobby_web.js: the browser implementation of wclobby/include/wclobby.h
 *  for the Emscripten build, on top of lobbylink's browser client
 *  (lobbylink/clients/ts; the page passes its P2PGame class in as
 *  Module.P2PGame).  Linked with --js-library, so the C++ transport in
 *  src/cpu/wcnet_lobby.cpp is the same code as in the native build.
 *
 *  It mirrors the state machine of wclobby/src/lib.rs: occupied slots,
 *  link states, generations, one inbox per slot, and a zero-length
 *  reliable message as the hangup marker.
 *
 *  Blocking calls (connect / open / accept / recv) suspend the wasm with
 *  Asyncify: the stack is unwound, the browser's event loop runs and
 *  delivers WebRTC and lobby events, and a waiter rewinds the stack once
 *  its condition holds or its timeout expires.  Everything a blocking
 *  function does happens inside the handleSleep callback, because the
 *  JS function itself is called a second time when the stack rewinds.
 */
addToLibrary({
  $WCLOBBY__deps: ['malloc', 'free', '$UTF8ToString', '$stringToUTF8', '$lengthBytesUTF8'],
  $WCLOBBY: {
    LOG_ERROR: 0, LOG_INFO: 1, LOG_EVENT: 2,
    PEER_ABSENT: 0, PEER_DOWN: 1, PEER_UP: 2, PEER_GONE: 3,
    LINK_DOWN: 0, LINK_UP: 1, LINK_FLAKY: 2, LINK_GONE: 3,
    hubs: {},
    nextHandle: 1,
    logFn: 0,

    log(level, msg) {
      if (WCLOBBY.logFn) {
        var len = lengthBytesUTF8(msg) + 1;
        var p = _malloc(len);
        stringToUTF8(msg, p, len);
        {{{ makeDynCall('vip', 'WCLOBBY.logFn') }}}(level, p);
        _free(p);
      } else {
        console.log('wclobby: ' + msg);
      }
    },

    hub(h) { return WCLOBBY.hubs[h]; },

    newPeer() {
      return { occupied: false, link: WCLOBBY.LINK_DOWN, gen: 0, attached: false, inbox: [], beInbox: [] };
    },
    live(p) {
      return p.occupied && (p.link === WCLOBBY.LINK_UP || p.link === WCLOBBY.LINK_FLAKY);
    },
    stateCode(p) {
      if (!p.occupied) return WCLOBBY.PEER_ABSENT;
      if (p.link === WCLOBBY.LINK_DOWN) return WCLOBBY.PEER_DOWN;
      if (p.link === WCLOBBY.LINK_GONE) return WCLOBBY.PEER_GONE;
      return WCLOBBY.PEER_UP;
    },
    // New generation: nothing the C side holds for this slot is valid any more.
    newGeneration(p) {
      p.gen = (p.gen + 1) >>> 0;
      p.attached = false;
      p.inbox.length = 0;
      p.beInbox.length = 0;
    },

    linkUp(hub, id) {
      var p = hub.peers[id];
      if (!p) return;
      if (p.link === WCLOBBY.LINK_UP) return;
      if (p.link === WCLOBBY.LINK_FLAKY) {
        p.link = WCLOBBY.LINK_UP;
        WCLOBBY.log(1, 'player ' + id + ': link recovered');
      } else {
        p.link = WCLOBBY.LINK_UP;
        WCLOBBY.newGeneration(p);
        WCLOBBY.log(1, 'player ' + id + ': link up');
      }
      WCLOBBY.notify(hub);
    },
    linkGone(hub, id, why) {
      var p = hub.peers[id];
      if (!p || p.link === WCLOBBY.LINK_GONE) return;
      var wasLive = WCLOBBY.live(p);
      p.link = WCLOBBY.LINK_GONE;
      WCLOBBY.newGeneration(p);
      if (wasLive) WCLOBBY.log(1, 'player ' + id + ': link lost (' + why + ')');
      WCLOBBY.notify(hub);
    },
    // A fresh peer connection is negotiating: any live link we still
    // believe in belonged to a previous connection.
    linkForming(hub, id) {
      var p = hub.peers[id];
      if (!p) return;
      if (WCLOBBY.live(p)) WCLOBBY.linkGone(hub, id, 'rebuilt');
      p.link = WCLOBBY.LINK_DOWN;
      WCLOBBY.notify(hub);
    },
    playerPresent(hub, id, what) {
      WCLOBBY.linkForming(hub, id);
      var p = hub.peers[id];
      if (p) {
        p.occupied = true;
        WCLOBBY.log(1, 'player ' + id + ' ' + what);
      }
      WCLOBBY.notify(hub);
    },
    playerAbsent(hub, id, why) {
      WCLOBBY.linkGone(hub, id, why);
      var p = hub.peers[id];
      if (p) {
        p.occupied = false;
        WCLOBBY.log(1, 'player ' + id + ' left (' + why + ')');
      }
      WCLOBBY.notify(hub);
    },
    // Reliable messages starting with "WCL\x01" belong to the page's lobby
    // (chat, presence); a protobuf message can never start with 0x57 (wire
    // type 7), so the game protocol is unaffected.
    isLobbyMessage(data) {
      return data.length >= 4 && data[0] === 0x57 && data[1] === 0x43 && data[2] === 0x4c && data[3] === 0x01;
    },
    deliver(hub, from, data) {
      var p = hub.peers[from];
      if (!p) return;
      if (WCLOBBY.isLobbyMessage(data)) return;
      if (data.length === 0) {
        // Hangup marker from the peer.
        WCLOBBY.log(p.attached ? 1 : 2, 'player ' + from + ' hung up' + (p.attached ? '' : ' before we accepted'));
        WCLOBBY.newGeneration(p);
        WCLOBBY.notify(hub);
        return;
      }
      // Data can only flow over an open channel; if we have not heard
      // "connected" yet, this is as good as that.
      WCLOBBY.linkUp(hub, from);
      p.inbox.push(data);
      WCLOBBY.notify(hub);
    },
    // A best-effort datagram: the newest 64 are kept for wclobby_recv_best_effort.
    deliverBestEffort(hub, from, data) {
      var p = hub.peers[from];
      if (!p || data.length === 0) return;
      WCLOBBY.linkUp(hub, from);
      if (p.beInbox.length >= 64) p.beInbox.shift();
      p.beInbox.push(data);
    },
    closeAll(hub) {
      hub.closed = true;
      for (var i = 0; i < hub.peers.length; i++) WCLOBBY.linkGone(hub, i, 'lobby closed');
      WCLOBBY.notify(hub);
    },

    handleEvent(hub, ev) {
      switch (ev.type) {
        case 'message':
          if (ev.kind === 'reliable') WCLOBBY.deliver(hub, ev.from, ev.data);
          else if (ev.kind === 'best-effort') WCLOBBY.deliverBestEffort(hub, ev.from, ev.data);
          break;
        case 'player-joined':
          WCLOBBY.playerPresent(hub, ev.playerId, 'joined');
          break;
        case 'player-rejoined':
          WCLOBBY.playerPresent(hub, ev.playerId, 'rejoined');
          break;
        case 'player-replaced':
          WCLOBBY.playerPresent(hub, ev.playerId, 'was replaced');
          break;
        case 'player-left':
          if (ev.reason === 'explicit-leave') {
            WCLOBBY.playerAbsent(hub, ev.playerId, 'left the room');
          } else {
            // Only the player's signaling socket is gone; the lobby keeps
            // the slot and the WebRTC link is unaffected.
            WCLOBBY.log(1, 'player ' + ev.playerId + ' lost its lobby connection; game link unaffected');
          }
          break;
        case 'started':
          WCLOBBY.log(2, 'room started');
          break;
        case 'peer-state':
          WCLOBBY.log(2, 'player ' + ev.playerId + ': peer connection ' + ev.state);
          if (ev.state === 'connected') {
            WCLOBBY.linkUp(hub, ev.playerId);
          } else if (ev.state === 'disconnected') {
            var p = hub.peers[ev.playerId];
            if (p && p.link === WCLOBBY.LINK_UP) p.link = WCLOBBY.LINK_FLAKY;
          } else if (ev.state === 'failed' || ev.state === 'closed') {
            WCLOBBY.linkGone(hub, ev.playerId, ev.state);
          } else {
            WCLOBBY.linkForming(hub, ev.playerId);
          }
          break;
        case 'candidate-pair':
          WCLOBBY.log(1, 'player ' + ev.playerId + ': path ' + ev.local + '/' + ev.remote);
          break;
        case 'lobby-error':
          WCLOBBY.log(0, 'lobby error ' + ev.code + ': ' + ev.message);
          break;
        case 'signaling-closed':
          if (ev.code === 'replaced' || ev.code === 'session-superseded' || ev.code === 'room-expired') {
            WCLOBBY.log(0, 'lobby session over (' + ev.code + ': ' + ev.message + ')');
            WCLOBBY.closeAll(hub);
          } else {
            WCLOBBY.log(0, 'lobby signaling lost (' + ev.code + ': ' + ev.message + '); existing links continue, nobody new can join');
            hub.signaling = false;
          }
          break;
      }
    },

    // Wakes every waiter whose condition now holds.  Rewinding the wasm
    // needs a clean JS stack, so when compiled code is running (we were
    // called from C, or from a nested export) it is retried from a timer.
    notify(hub) {
      if (!hub.waiters.length) return;
      if (Asyncify.state !== Asyncify.State.Normal || Asyncify.exportCallStack.length) {
        if (!hub.notifyPending) {
          hub.notifyPending = true;
          setTimeout(function() { hub.notifyPending = false; WCLOBBY.notify(hub); }, 0);
        }
        return;
      }
      var i = 0;
      while (i < hub.waiters.length) {
        var w = hub.waiters[i];
        var r = w.check();
        if (r === undefined) { i++; continue; }
        hub.waiters.splice(i, 1);
        if (w.timer) clearTimeout(w.timer);
        w.wakeUp(r);
      }
    },
    // Runs `check` until it returns a value (then that is the result) or the
    // timeout expires (result 0).  timeoutMs < 0 waits forever, 0 polls.
    waitUntil(hub, timeoutMs, check, wakeUp) {
      var r = check();
      if (r !== undefined) { wakeUp(r); return; }
      if (timeoutMs === 0) { wakeUp(0); return; }
      var w = { check: check, wakeUp: wakeUp, timer: null };
      if (timeoutMs > 0) {
        w.timer = setTimeout(function() {
          var i = hub.waiters.indexOf(w);
          if (i >= 0) { hub.waiters.splice(i, 1); w.wakeUp(0); }
        }, timeoutMs);
      }
      hub.waiters.push(w);
    },
  },

  wclobby_set_logger__deps: ['$WCLOBBY'],
  wclobby_set_logger: (fn) => { WCLOBBY.logFn = fn; },

  wclobby_connect__deps: ['$WCLOBBY'],
  wclobby_connect__async: true,
  wclobby_connect: (opts, err, errLen) => Asyncify.handleSleep((wakeUp) => {
    var server = UTF8ToString({{{ makeGetValue('opts', 0, '*') }}});
    var code = UTF8ToString({{{ makeGetValue('opts', 4, '*') }}});
    var maxPlayers = HEAPU16[(opts + 12) >> 1];
    var forceRelay = {{{ makeGetValue('opts', 16, 'i32') }}} !== 0;
    var fail = (msg) => {
      WCLOBBY.log(0, 'joining room ' + code + ' failed: ' + msg);
      if (err && errLen > 0) stringToUTF8(msg, err, errLen);
      wakeUp(0);
    };
    var P2PGame = Module['P2PGame'];
    if (!P2PGame) {
      fail('no-client: the page did not provide the lobbylink client (Module.P2PGame)');
      return;
    }
    var adopt = (game, how) => {
      var hub = { game: game, code: code, closed: false, signaling: true, peers: [], waiters: [],
                  notifyPending: false, selfId: game.selfId, maxPlayers: game.maxPlayers, adopted: how === 'adopted' };
      for (var i = 0; i < game.maxPlayers; i++) hub.peers.push(WCLOBBY.newPeer());
      game.players.forEach((p) => {
        if (p.occupied && hub.peers[p.id]) hub.peers[p.id].occupied = true;
        // A link the page already brought up is live for us as well.
        if (p.connected && p.id !== game.selfId && hub.peers[p.id] && how === 'adopted') hub.peers[p.id].link = WCLOBBY.LINK_UP;
      });
      game.onEvent((ev) => WCLOBBY.handleEvent(hub, ev));
      var h = WCLOBBY.nextHandle++;
      WCLOBBY.hubs[h] = hub;
      WCLOBBY.log(1, how + ' room ' + code + ' as player ' + game.selfId + ' of ' + game.maxPlayers);
      // Game messages the page received before we existed (a client that
      // reached its launch first): deliver them now, in order.
      if (how === 'adopted' && Module['takeLobbyBacklog']) {
        var backlog = Module['takeLobbyBacklog']() || [];
        if (backlog.length) WCLOBBY.log(1, backlog.length + ' message(s) arrived before the game was ready');
        backlog.forEach((m) => WCLOBBY.deliver(hub, m.from, m.data));
      }
      wakeUp(h);
    };
    // The page may already be in the room (its lobby with roster and chat):
    // use that connection rather than joining twice, which would supersede
    // the page's session.  A page that manages rooms (Module.lobbyGame: the
    // connection, or a function giving the one the page is in now) is the
    // only way into a room: with no live connection to this one the game
    // does not join behind the page's back.  (2026-10-15: a wingman whose
    // host's link had failed adopted the page's closed connection again
    // every few seconds, each time to lose it at once.)
    var pageHasRooms = Module['lobbyGame'] !== undefined;
    var live = typeof Module['lobbyGame'] === 'function' ? Module['lobbyGame']() : Module['lobbyGame'];
    if (live && live.code === code && !live.closedFlag) {
      adopt(live, 'adopted');
      return;
    }
    if (pageHasRooms) {
      fail(live && live.code === code ? 'closed: the page has left room ' + code : 'no-room: the page is not in room ' + code);
      return;
    }
    WCLOBBY.log(1, 'joining room ' + code + ' at ' + server);
    var connectOpts = { server: server, code: code, storage: 'session', storageKey: 'wclobby-' + code };
    if (maxPlayers > 0) {
      connectOpts.create = { maxPlayers: maxPlayers, waitUntilFull: false, allowLateJoin: true,
                             allowReconnect: true, allowReplacement: true };
    }
    if (forceRelay) connectOpts.forceRelay = true;
    P2PGame.connect(connectOpts).then((game) => adopt(game, 'joined'), (e) => {
      fail((e && e.code ? e.code : 'error') + ': ' + (e && e.message ? e.message : String(e)));
    });
  }),

  wclobby_close__deps: ['$WCLOBBY'],
  wclobby_close: (h) => {
    var hub = WCLOBBY.hub(h);
    if (!hub) return;
    delete WCLOBBY.hubs[h];
    try { hub.game.close(); } catch (e) { /* already gone */ }
    hub.signaling = false;
    WCLOBBY.closeAll(hub);
    if (hub.adopted && Module['onLobbyClosed']) setTimeout(() => Module['onLobbyClosed'](), 0);
  },

  wclobby_self_id__deps: ['$WCLOBBY'],
  wclobby_self_id: (h) => { var hub = WCLOBBY.hub(h); return hub ? hub.selfId : 0; },
  wclobby_max_players__deps: ['$WCLOBBY'],
  wclobby_max_players: (h) => { var hub = WCLOBBY.hub(h); return hub ? hub.maxPlayers : 0; },
  wclobby_signaling_alive__deps: ['$WCLOBBY'],
  wclobby_signaling_alive: (h) => { var hub = WCLOBBY.hub(h); return (hub && hub.signaling && !hub.closed) ? 1 : 0; },

  wclobby_peer_state__deps: ['$WCLOBBY'],
  wclobby_peer_state: (h, player, genOut) => {
    var hub = WCLOBBY.hub(h);
    var p = hub && hub.peers[player];
    if (!p) return WCLOBBY.PEER_ABSENT;
    if (genOut) HEAPU32[genOut >> 2] = p.gen;
    return WCLOBBY.stateCode(p);
  },

  wclobby_open__deps: ['$WCLOBBY'],
  wclobby_open__async: true,
  wclobby_open: (h, player, timeoutMs, genOut) => Asyncify.handleSleep((wakeUp) => {
    var hub = WCLOBBY.hub(h);
    if (!hub) { wakeUp(-1); return; }
    WCLOBBY.waitUntil(hub, timeoutMs, () => {
      if (hub.closed) return -1;
      var p = hub.peers[player];
      if (!p || !p.occupied) return -1;
      if (WCLOBBY.live(p)) {
        p.attached = true;
        if (genOut) HEAPU32[genOut >> 2] = p.gen;
        return 1;
      }
      return undefined;
    }, wakeUp);
  }),

  wclobby_accept__deps: ['$WCLOBBY'],
  wclobby_accept__async: true,
  wclobby_accept: (h, timeoutMs, playerOut, genOut) => Asyncify.handleSleep((wakeUp) => {
    var hub = WCLOBBY.hub(h);
    if (!hub) { wakeUp(-1); return; }
    WCLOBBY.waitUntil(hub, timeoutMs, () => {
      if (hub.closed) return -1;
      for (var id = 0; id < hub.peers.length; id++) {
        var p = hub.peers[id];
        if (WCLOBBY.live(p) && !p.attached && p.inbox.length) {
          p.attached = true;
          if (playerOut) HEAPU16[playerOut >> 1] = id;
          if (genOut) HEAPU32[genOut >> 2] = p.gen;
          return 1;
        }
      }
      return undefined;
    }, wakeUp);
  }),

  wclobby_is_open__deps: ['$WCLOBBY'],
  wclobby_is_open: (h, player, gen) => {
    var hub = WCLOBBY.hub(h);
    if (!hub || hub.closed) return 0;
    var p = hub.peers[player];
    return (p && p.gen === (gen >>> 0) && WCLOBBY.live(p) && p.attached) ? 1 : 0;
  },

  wclobby_send__deps: ['$WCLOBBY'],
  wclobby_send: (h, to, gen, data, len) => {
    var hub = WCLOBBY.hub(h);
    if (!hub || hub.closed || len === 0) return -1;
    var p = hub.peers[to];
    if (!p || p.gen !== (gen >>> 0) || !WCLOBBY.live(p)) return -1;
    var bytes = HEAPU8.slice(data, data + len);
    hub.game.sendReliable(to, bytes).catch((e) => {
      WCLOBBY.log(0, 'send to player ' + to + ' failed: ' + (e && e.message ? e.message : String(e)));
      WCLOBBY.linkGone(hub, to, 'send failed');
    });
    return 0;
  },

  wclobby_send_best_effort__deps: ['$WCLOBBY'],
  wclobby_send_best_effort: (h, to, gen, data, len) => {
    var hub = WCLOBBY.hub(h);
    if (!hub || hub.closed || len === 0) return -1;
    var p = hub.peers[to];
    if (!p || p.gen !== (gen >>> 0) || !WCLOBBY.live(p)) return -1;
    try {
      hub.game.sendBestEffort(to, HEAPU8.slice(data, data + len));
    } catch (e) {
      // (A closed or full channel drops it: the contract.  Only a caller's
      // error throws, and that is worth a line.)
      WCLOBBY.log(2, 'best-effort send to player ' + to + ' failed: ' + (e && e.message ? e.message : String(e)));
    }
    return 0;
  },

  wclobby_recv_best_effort__deps: ['$WCLOBBY'],
  wclobby_recv_best_effort: (h, from, gen, out) => {
    var hub = WCLOBBY.hub(h);
    if (!hub || hub.closed) return -1;
    var p = hub.peers[from];
    if (!p || p.gen !== (gen >>> 0)) return -1;
    if (p.beInbox.length) {
      var bytes = p.beInbox.shift();
      var ptr = _malloc(bytes.length || 1);
      HEAPU8.set(bytes, ptr);
      HEAPU32[out >> 2] = ptr;
      HEAPU32[(out + 4) >> 2] = bytes.length;
      return 1;
    }
    return WCLOBBY.live(p) ? 0 : -1;
  },

  wclobby_recv__deps: ['$WCLOBBY'],
  wclobby_recv__async: true,
  wclobby_recv: (h, from, gen, timeoutMs, out) => Asyncify.handleSleep((wakeUp) => {
    var hub = WCLOBBY.hub(h);
    if (!hub) { wakeUp(-1); return; }
    WCLOBBY.waitUntil(hub, timeoutMs, () => {
      if (hub.closed) return -1;
      var p = hub.peers[from];
      if (!p) return undefined;
      if (p.gen !== (gen >>> 0)) return -1;
      if (p.inbox.length) {
        var bytes = p.inbox.shift();
        var ptr = _malloc(bytes.length || 1);
        HEAPU8.set(bytes, ptr);
        HEAPU32[out >> 2] = ptr;
        HEAPU32[(out + 4) >> 2] = bytes.length;
        return 1;
      }
      if (!WCLOBBY.live(p)) return -1;
      return undefined;
    }, wakeUp);
  }),

  wclobby_buf_free__deps: ['$WCLOBBY'],
  wclobby_buf_free: (buf) => {
    if (!buf) return;
    var ptr = HEAPU32[buf >> 2];
    if (ptr) _free(ptr);
    HEAPU32[buf >> 2] = 0;
    HEAPU32[(buf + 4) >> 2] = 0;
  },

  wclobby_hangup__deps: ['$WCLOBBY'],
  wclobby_hangup: (h, player, gen) => {
    var hub = WCLOBBY.hub(h);
    if (!hub) return;
    var p = hub.peers[player];
    if (!p || p.gen !== (gen >>> 0) || !p.attached) return;
    var wasLive = WCLOBBY.live(p);
    WCLOBBY.newGeneration(p);
    WCLOBBY.log(1, 'hanging up on player ' + player);
    if (wasLive && !hub.closed) {
      hub.game.sendReliable(player, new Uint8Array(0)).catch(() => { /* link already gone */ });
    }
    WCLOBBY.notify(hub);
  },
});

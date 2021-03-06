#include "node.hpp"
#include "common.hpp"

Node::Node(uint8_t node_id, const uint8_t *key)
    : radio(CE_PIN, CS_PIN), network(radio), mesh(radio, network),
      node_id(node_id), key(key), session(micros()), own_id(0), other_id(0),
      last_pong(millis()) {}

void Node::init() {
  if (node_id != mesh.getNodeID()) {
    DEBUG_LOG("Set node id to: %x", node_id);
    mesh.setNodeID(node_id);
  } else {
    DEBUG_LOG("Node id already set.");
  }
  network.setup_watchdog(6);

  DEBUG_LOG("Have node id: %x", mesh.getNodeID());
  mesh.begin();

  DEBUG_LOG("Update...");
  update();
  session = micros();
  DEBUG_LOG("Boot Sessionid is %u", session);

  Message *boot_message = prepareSendMessage();
  boot_message->setShort(millis());

  DEBUG_LOG("Send booted packet");
  if (!send(MASTER_ADDR, booted, boot_message)) {
    // Mesh not available? try again?.
    DEBUG_LOG("Send of booted failed");
    delay(1000);
    RESET();
  }

  DEBUG_LOG("Sent ... ");
  uint16_t last = millis();

  for (;;) {
    Message *recieve_message = prepareRecieveMessage();
    node_t sender;
    messages_t type;

    // network.sleepNode(1, 0);
    update();

    if (0 != fetch(&sender, &type, recieve_message)) {
      DEBUG_LOG("Have data");
      last = millis();
      switch (type) {
      case configure:
        registry.configure(recieve_message);
        sendPong();
        last = millis();
        break;
      case configured:
        setSession(recieve_message->getShort());
        sendPong();
        return;
      default:
        DEBUG_LOG("Got an unexpected message from %x with type %x", sender,
                  type);
        break;
      }
      continue;
    }

    if (last + CONFIG_TIMEOUT < millis()) {
      // reset if we did not get an answer.
      DEBUG_LOG("Reset controller .. ");
      RESET();
    }
  }
}

void Node::update() { mesh.update(); }

void Node::checkConn() {
  if (!mesh.checkConnection()) {
    // refresh the network address
    mesh.renewAddress();
  }
}

msg_size_t Node::fetch(uint16_t *sender, messages_t *type, Message *msg) {
  // DEBUG_LOG("Check packet availability");
  if (network.available()) {
    DEBUG_LOG("Packet available");
    RF24NetworkHeader header;
    network.peek(header);

    *sender = mesh.getNodeID(header.from_node);
    *type = (messages_t)header.type;
    uint16_t receiver = mesh.getNodeID(header.to_node);

    // Read the data
    uint16_t size = network.read(header, msg->rawBuffer(), Message::maxLen());

    DEBUG_LOG("Got packet from %x to %x (%u bytes)", *sender, receiver, size);
    if (!msg->verify(key, *sender, receiver, header.type, size)) {
      DEBUG_LOG("Cannot verify message");
      return 0;
    }

    session_t pkt_session = msg->getShort();
    if (pkt_session != session) {
      DEBUG_LOG("Wrong session: %u != %u", pkt_session, session);
      return 0;
    }

    /**
      * reset the controller on reset request message.
      *
      * We do not check the counter here as the sender may
      * have restarted and lost its state.
      *
      * Should be enough to check the session id as this
      * can be adapted from the sender from our pong packets
      * and as we restart after this packet and negotiate a
      * new session replay should not be a problem.
      */
    if (*type == reset) {
      DEBUG_LOG("Reset controller after reset message");
      RESET();
    }

    /* check that we cot a packet with an incremented counter */
    counter_t pkt_cnt = msg->getShort();
    if (pkt_cnt <= other_id) {
      DEBUG_LOG("Wrong counter: %d", pkt_cnt);
      return 0;
    }
    other_id = pkt_cnt;

    return msg->len();
  } else {
    return 0;
  }
}

bool Node::send(uint16_t reciever, messages_t type, Message *msg) {
  uint16_t len = msg->finalize(key, node_id, reciever, type);

  for (uint8_t i = 0; i < 3; ++i) {

    // Write the message
    if (mesh.write(reciever, msg->rawBuffer(), type, len)) {
      return true;
    } else {
      checkConn();
    }
  }
  return false;
}

void Node::setSession(session_t _session) {
  session = _session;
  own_id = 0;
  other_id = 0;
}

Message *Node::prepareSendMessage() {
  message.init(session, own_id++);
  return &message;
}

Message *Node::prepareRecieveMessage() {
  message.reset();
  return &message;
}

void Node::process() {
  update();

  // Handle incomming packets
  {
    Message *recieved = prepareRecieveMessage();
    uint16_t sender;
    messages_t type;
    msg_size_t recieved_len = fetch(&sender, &type, recieved);

    if (recieved_len > 0) {
      session_t new_session;

      switch (type) {
      case set_state:
        registry.setState(recieved);
        return;
      case get_state:
        registry.requestState(recieved);
        return;
      case ping:
        new_session = recieved->getShort();
        if (new_session != session) {
          setSession(new_session);
          sendPong();
          last_pong = millis();
        }
        return;
      // Messages that should not arrive here.
      case pong:
      case booted:
      case configure:
      case configured:
      case reading:
      case reset:
      default:
        DEBUG_LOG("Got an unexpected message from %x with type %x", sender,
                  type);
      }
    }
  }

  // Send pong
  if (last_pong + PONG_INTERVAL < millis()) {
    sendPong();
    last_pong = millis();
    return;
  }

  // Send out state
  Message *state_packet = prepareSendMessage();
  if (registry.nextState(state_packet)) {
    return;
  }

  // Update state of items
  if (last_check + CHECK_INTERVAL < millis()) {
    registry.checkItems();
    last_check = millis();
  }
}

void Node::sendPong() {
  Message *pong_message = prepareSendMessage();
  pong_message->setShort(millis());
  send(MASTER_ADDR, pong, pong_message);
}

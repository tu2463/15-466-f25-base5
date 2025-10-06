#include "Game.hpp"

#include "Connection.hpp"

#include <stdexcept>
#include <iostream>
#include <cstring>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

void Player::Controls::send_controls_message(Connection *connection_) const
{
	assert(connection_);
	auto &connection = *connection_;

	uint32_t size = 5;
	connection.send(Message::C2S_Controls);
	connection.send(uint8_t(size));
	connection.send(uint8_t(size >> 8));
	connection.send(uint8_t(size >> 16));

	auto send_button = [&](Button const &b)
	{
		if (b.downs & 0x80)
		{
			std::cerr << "Wow, you are really good at pressing buttons!" << std::endl;
		}
		connection.send(uint8_t((b.pressed ? 0x80 : 0x00) | (b.downs & 0x7f)));
	};

	send_button(left);
	send_button(right);
	send_button(up);
	send_button(down);
	send_button(jump);
}

bool Player::Controls::recv_controls_message(Connection *connection_)
{
	assert(connection_);
	auto &connection = *connection_;

	auto &recv_buffer = connection.recv_buffer;

	// expecting [type, size_low0, size_mid8, size_high8]:
	if (recv_buffer.size() < 4)
		return false;
	if (recv_buffer[0] != uint8_t(Message::C2S_Controls))
		return false;
	uint32_t size = (uint32_t(recv_buffer[3]) << 16) | (uint32_t(recv_buffer[2]) << 8) | uint32_t(recv_buffer[1]);
	if (size != 5)
		throw std::runtime_error("Controls message with size " + std::to_string(size) + " != 5!");

	// expecting complete message:
	if (recv_buffer.size() < 4 + size)
		return false;

	auto recv_button = [](uint8_t byte, Button *button)
	{
		button->pressed = (byte & 0x80);
		uint32_t d = uint32_t(button->downs) + uint32_t(byte & 0x7f);
		if (d > 255)
		{
			std::cerr << "got a whole lot of downs" << std::endl;
			d = 255;
		}
		button->downs = uint8_t(d);
	};

	recv_button(recv_buffer[4 + 0], &left);
	recv_button(recv_buffer[4 + 1], &right);
	recv_button(recv_buffer[4 + 2], &up);
	recv_button(recv_buffer[4 + 3], &down);
	recv_button(recv_buffer[4 + 4], &jump);

	// delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}

//-----------------------------------------

Game::Game() : mt(0x15466666)
{
}

Player *Game::spawn_player()
{
	players.emplace_back();
	Player &player = players.back();

	// random point in the middle area of the arena:
	player.position.x = glm::mix(ArenaMin.x + 2.0f * PlayerRadius, ArenaMax.x - 2.0f * PlayerRadius, 0.4f + 0.2f * mt() / float(mt.max()));
	player.position.y = glm::mix(ArenaMin.y + 2.0f * PlayerRadius, ArenaMax.y - 2.0f * PlayerRadius, 0.4f + 0.2f * mt() / float(mt.max()));

	do
	{
		player.color.r = mt() / float(mt.max());
		player.color.g = mt() / float(mt.max());
		player.color.b = mt() / float(mt.max());
	} while (player.color == glm::vec3(0.0f));
	player.color = glm::normalize(player.color);

	player.name = "Player " + std::to_string(next_player_number++);

	return &player;
}

void Game::remove_player(Player *player)
{
	bool found = false;
	for (auto pi = players.begin(); pi != players.end(); ++pi)
	{
		if (&*pi == player)
		{
			players.erase(pi);
			found = true;
			break;
		}
	}
	assert(found);
}

void Game::update(float elapsed)
{
	// position/velocity update:
	for (auto &p : players)
	{
		glm::vec2 dir = glm::vec2(0.0f, 0.0f);
		if (p.controls.left.pressed)
			dir.x -= 1.0f;
		if (p.controls.right.pressed)
			dir.x += 1.0f;
		if (p.controls.down.pressed)
			dir.y -= 1.0f;
		if (p.controls.up.pressed)
			dir.y += 1.0f;

		if (dir == glm::vec2(0.0f))
		{
			// no inputs: just drift to a stop
			float amt = 1.0f - std::pow(0.5f, elapsed / (PlayerAccelHalflife * 2.0f));
			p.velocity = glm::mix(p.velocity, glm::vec2(0.0f, 0.0f), amt);
		}
		else
		{
			// inputs: tween velocity to target direction
			dir = glm::normalize(dir);

			float amt = 1.0f - std::pow(0.5f, elapsed / PlayerAccelHalflife);

			// accelerate along velocity (if not fast enough):
			float along = glm::dot(p.velocity, dir);
			if (along < PlayerSpeed)
			{
				along = glm::mix(along, PlayerSpeed, amt);
			}

			// damp perpendicular velocity:
			float perp = glm::dot(p.velocity, glm::vec2(-dir.y, dir.x));
			perp = glm::mix(perp, 0.0f, amt);

			p.velocity = dir * along + glm::vec2(-dir.y, dir.x) * perp;
		}
		p.position += p.velocity * elapsed;

		// reset 'downs' since controls have been handled:
		p.controls.left.downs = 0;
		p.controls.right.downs = 0;
		p.controls.up.downs = 0;
		p.controls.down.downs = 0;
		p.controls.jump.downs = 0;
	}

	// collision resolution:
	for (auto &p1 : players)
	{
		// player/player collisions:
		for (auto &p2 : players)
		{
			if (&p1 == &p2)
				break;
			glm::vec2 p12 = p2.position - p1.position;
			float len2 = glm::length2(p12);
			if (len2 > (2.0f * PlayerRadius) * (2.0f * PlayerRadius))
				continue;
			if (len2 == 0.0f)
				continue;
			glm::vec2 dir = p12 / std::sqrt(len2);
			// mirror velocity to be in separating direction:
			glm::vec2 v12 = p2.velocity - p1.velocity;
			glm::vec2 delta_v12 = dir * glm::max(0.0f, -1.75f * glm::dot(dir, v12));
			p2.velocity += 0.5f * delta_v12;
			p1.velocity -= 0.5f * delta_v12;
		}
		// player/arena collisions:
		if (p1.position.x < ArenaMin.x + PlayerRadius)
		{
			p1.position.x = ArenaMin.x + PlayerRadius;
			p1.velocity.x = std::abs(p1.velocity.x);
		}
		if (p1.position.x > ArenaMax.x - PlayerRadius)
		{
			p1.position.x = ArenaMax.x - PlayerRadius;
			p1.velocity.x = -std::abs(p1.velocity.x);
		}
		if (p1.position.y < ArenaMin.y + PlayerRadius)
		{
			p1.position.y = ArenaMin.y + PlayerRadius;
			p1.velocity.y = std::abs(p1.velocity.y);
		}
		if (p1.position.y > ArenaMax.y - PlayerRadius)
		{
			p1.position.y = ArenaMax.y - PlayerRadius;
			p1.velocity.y = -std::abs(p1.velocity.y);
		}
	}
}

void Game::send_state_message(Connection *connection_, Player *connection_player) const
{
	assert(connection_);
	auto &connection = *connection_;

	connection.send(Message::S2C_State);
	// will patch message size in later, for now placeholder bytes:
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	size_t mark = connection.send_buffer.size(); // keep track of this position in the buffer

	connection.send(uint8_t(phase));

	// Determine which player this connection is, for self_index
    uint8_t idx = 0;
    if (connection_player && players.size() >= 1) {
        if (connection_player == &players.front()) idx = 1;
        else if (players.size() >= 2) idx = 2;
    }
    connection.send(idx);    

	// send player info helper:
	auto send_player = [&](Player const &player)
	{
		connection.send(player.position);
		connection.send(player.velocity);
		connection.send(player.color);

		// NOTE: can't just 'send(name)' because player.name is not plain-old-data type.
		// effectively: truncates player name to 255 chars
		uint8_t len = uint8_t(std::min<size_t>(255, player.name.size()));
		connection.send(len);
		connection.send_buffer.insert(connection.send_buffer.end(), player.name.begin(), player.name.begin() + len);
	};

	// player count:
	connection.send(uint8_t(players.size()));
	if (connection_player)
		send_player(*connection_player);
	for (auto const &player : players)
	{
		if (&player == connection_player)
			continue;
		send_player(player);
	}

	// Phase-specific states
	if (phase == Phase::Lobby)
	{
		connection.send(role_1);
		connection.send(role_2);
		connection.send(selected_role_1);
		connection.send(selected_role_2);
	}
	if (phase == Phase::Operation)
	{
		uint16_t N = (uint16_t)std::min<size_t>(65535, corrupted_instruction.size());
		connection.send(N);
		connection.send_buffer.insert(connection.send_buffer.end(),
									  corrupted_instruction.begin(), corrupted_instruction.begin() + N);
		connection.send(found_count);
		connection.send(attempt_count);
	}

	// compute the message size and patch into the message header:
	uint32_t size = uint32_t(connection.send_buffer.size() - mark);
	connection.send_buffer[mark - 3] = uint8_t(size);
	connection.send_buffer[mark - 2] = uint8_t(size >> 8);
	connection.send_buffer[mark - 1] = uint8_t(size >> 16);
}

bool Game::recv_state_message(Connection *connection_)
{
	assert(connection_);
	auto &connection = *connection_;
	auto &recv_buffer = connection.recv_buffer;

	if (recv_buffer.size() < 4)
		return false;
	if (recv_buffer[0] != uint8_t(Message::S2C_State))
		return false;
	uint32_t size = (uint32_t(recv_buffer[3]) << 16) | (uint32_t(recv_buffer[2]) << 8) | uint32_t(recv_buffer[1]);
	uint32_t at = 0;
	// expecting complete message:
	if (recv_buffer.size() < 4 + size)
		return false;

	// copy bytes from buffer and advance position:
	auto read = [&](auto *val)
	{
		if (at + sizeof(*val) > size)
		{
			throw std::runtime_error("Ran out of bytes reading state message.");
		}
		std::memcpy(val, &recv_buffer[4 + at], sizeof(*val));
		at += sizeof(*val);
	};

	// Read phase
	uint8_t ph;
    read(&ph);
    phase = Phase(ph);

	read(&self_index);

	players.clear();
	uint8_t player_count;
	read(&player_count);
	for (uint8_t i = 0; i < player_count; ++i)
	{
		players.emplace_back();
		Player &player = players.back();
		read(&player.position);
		read(&player.velocity);
		read(&player.color);
		uint8_t name_len;
		read(&name_len);
		// n.b. would probably be more efficient to directly copy from recv_buffer, but I think this is clearer:
		player.name = "";
		for (uint8_t n = 0; n < name_len; ++n)
		{
			char c;
			read(&c);
			player.name += c;
		}
	}

	// Phase-specific states
	if (phase == Phase::Lobby) {
        read(&role_1);
        read(&role_2);
        read(&selected_role_1);
        read(&selected_role_2);
    }
	if (phase == Phase::Operation)
	{
		uint16_t N;
		read(&N);
		corrupted_instruction.resize(N);
		if (N)
			std::memcpy(&corrupted_instruction[0], &recv_buffer[4 + at], N);
		at += N;
		read(&found_count);
		read(&attempt_count);
	}

	if (at != size)
		throw std::runtime_error("Trailing data in state message.");

	// delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}

void Game::send_selected_role_message(Connection *connection_, uint8_t selected_0_or_1)
{
	assert(connection_);
	auto &connection = *connection_;
	connection.send(uint8_t(Message::C2S_SelectedRole));
	connection.send(uint8_t(1)); // payload size = 1
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	connection.send(uint8_t(selected_0_or_1 ? 1 : 0));
}

bool Game::recv_selected_role_message(Connection *connection_, uint8_t *out_selected)
{
	assert(connection_);
	auto &connection = *connection_;
	auto &rb = connection.recv_buffer;

	if (rb.size() < 4)
		return false;
	if (rb[0] != uint8_t(Message::C2S_SelectedRole))
		return false;

	uint32_t size = (uint32_t(rb[3]) << 16) | (uint32_t(rb[2]) << 8) | uint32_t(rb[1]);
	if (size != 1)
		throw std::runtime_error("SelectedRole message must have size 1");
	if (rb.size() < 4 + size)
		return false;

	if (out_selected)
		*out_selected = rb[4] ? 1 : 0;

	rb.erase(rb.begin(), rb.begin() + 4 + size);
	return true;
}

void Game::send_login_message(Connection *connection_, Role role)
{
	assert(connection_);
	auto &connection = *connection_;

	// [type, size_low0, size_mid8, size_high16]
	connection.send(uint8_t(Message::C2S_Login));
	connection.send(uint8_t(1)); // payload size = 1 byte
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));

	connection.send(uint8_t(role));
}

bool Game::recv_login_message(Connection *connection_, Role *out_role)
{
	assert(connection_);
	auto &connection = *connection_;
	auto &recv_buffer = connection.recv_buffer;

	// need at least header:
	if (recv_buffer.size() < 4)
		return false;
	if (recv_buffer[0] != uint8_t(Message::C2S_Login))
		return false;

	uint32_t size = (uint32_t(recv_buffer[3]) << 16) | (uint32_t(recv_buffer[2]) << 8) | uint32_t(recv_buffer[1]);
	if (size != 1)
	{
		throw std::runtime_error("Login message size must be 1, got " + std::to_string(size));
	}
	if (recv_buffer.size() < 4 + size)
		return false; // wait for full message

	uint8_t selected_role_index = recv_buffer[4];
	if (out_role)
		*out_role = Role(selected_role_index);

	// pop message
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);
	return true;
}

void Game::send_instruction_message(Connection *connection_, std::string const& utf8) {
    assert(connection_);
    auto &connection = *connection_;

    // payload = uint16 N + N bytes:
    uint16_t N = (uint16_t)std::min<size_t>(utf8.size(), 65535);

    connection.send(uint8_t(Message::C2S_Instruction));
    uint32_t payload = 2u + uint32_t(N);
    connection.send(uint8_t(payload));
    connection.send(uint8_t(payload >> 8));
    connection.send(uint8_t(payload >> 16));

    connection.send(N);
    connection.send_buffer.insert(connection.send_buffer.end(), utf8.begin(), utf8.begin() + N);
}

bool Game::recv_instruction_message(Connection *connection_, std::string* out_utf8) {
    assert(connection_);
    auto &connection = *connection_;
    auto &recv_buffer = connection.recv_buffer;

    if (recv_buffer.size() < 4) return false;
    if (recv_buffer[0] != uint8_t(Message::C2S_Instruction)) return false;

    uint32_t size = (uint32_t(recv_buffer[3]) << 16) | (uint32_t(recv_buffer[2]) << 8) | uint32_t(recv_buffer[1]);
    if (recv_buffer.size() < 4 + size) return false;

    if (size < 2) throw std::runtime_error("Instruction payload too small");
    uint16_t N;
    std::memcpy(&N, &recv_buffer[4], 2);
    if (2u + N != size) throw std::runtime_error("Instruction payload size mismatch");
    if (out_utf8) out_utf8->assign((char const*)&recv_buffer[6], (char const*)&recv_buffer[6] + N);

    recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);
    return true;
}

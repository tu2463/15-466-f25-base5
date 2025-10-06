#pragma once

#include <glm/glm.hpp>

#include <string>
#include <list>
#include <random>

struct Connection;

//Game state, separate from rendering.

//Currently set up for a "client sends controls" / "server sends whole state" situation.

enum class Message : uint8_t {
	C2S_Controls = 1, //Greg!
	S2C_State = 's',
	//...
	C2S_Login = 'L',
	C2S_Instruction = 'I',
	C2S_SelectedRole = 'R'
};

//used to represent a control input:
struct Button {
	uint8_t downs = 0; //times the button has been pressed
	bool pressed = false; //is the button pressed now
};

enum class Role : uint8_t { Unknown = 0, Communicator = 1, Operative = 2 };

//state of one player in the game:
struct Player {
	//player inputs (sent from client):
	struct Controls {
		Button left, right, up, down, jump;

		void send_controls_message(Connection *connection) const;

		//returns 'false' if no message or not a controls message,
		//returns 'true' if read a controls message,
		//throws on malformed controls message
		bool recv_controls_message(Connection *connection);
	} controls;

	//player state (sent from server):
	glm::vec2 position = glm::vec2(0.0f, 0.0f);
	glm::vec2 velocity = glm::vec2(0.0f, 0.0f);

	glm::vec3 color = glm::vec3(1.0f, 1.0f, 1.0f);
	std::string name = "";

	Role role = Role::Unknown;
};

struct Game {
	std::list< Player > players; //(using list so they can have stable addresses)
	Player *spawn_player(); //add player the end of the players list (may also, e.g., play some spawn anim)
	void remove_player(Player *); //remove player from game (may also, e.g., play some despawn anim)

	std::mt19937 mt; //used for spawning players
	uint32_t next_player_number = 1; //used for naming players

	Game();

	//state update function:
	void update(float elapsed);

	//constants:
	//the update rate on the server:
	inline static constexpr float Tick = 1.0f / 30.0f;

	//arena size:
	inline static constexpr glm::vec2 ArenaMin = glm::vec2(-0.75f, -1.0f);
	inline static constexpr glm::vec2 ArenaMax = glm::vec2( 0.75f,  1.0f);

	//player constants:
	inline static constexpr float PlayerRadius = 0.06f;
	inline static constexpr float PlayerSpeed = 2.0f;
	inline static constexpr float PlayerAccelHalflife = 0.25f;

	// --- Phase ---
	enum class Phase : uint8_t { Lobby = 0, Communication = 1, Operation = 2 };
    Phase phase = Phase::Lobby;

	//---- communication helpers ----

	//used by client:
	//set game state from data in connection buffer
	// (return true if data was read)
	bool recv_state_message(Connection *connection);

	//used by server:
	//send game state.
	//  Will move "connection_player" to the front of the front of the sent list.
	void send_state_message(Connection *connection, Player *connection_player = nullptr) const;

	// Lobby Phase
	uint8_t role_1 = 0; // Role enum: 0 Unknown, 1 Communicator, 2 Operative
	uint8_t role_2 = 0;
	uint8_t selected_role_1 = 0; // 0 = Communicator, 1 = Operative
	uint8_t selected_role_2 = 1;

	// broadcast self_index in the snapshot so each client can tell if it’s P1 or P2.
	uint8_t self_index = 0; // 0 = initial, 1 = Player 1, 2 = Player 2

	// update selected role
	static void send_selected_role_message(Connection *c, uint8_t selected_0_or_1);
	static bool recv_selected_role_message(Connection *c, uint8_t *out_selected);

	static void send_login_message(Connection *c, Role role);
    static bool recv_login_message(Connection *c, Role *out_role);

	// Communication Phase
	std::string instruction_text;
	static void send_instruction_message(Connection *c, std::string const &utf8);
	static bool recv_instruction_message(Connection *c, std::string *out_utf8);

	// Opreation Phase
	uint8_t found_count = 0;
	uint8_t attempt_count = 25;
};

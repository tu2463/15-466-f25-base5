
#include "Connection.hpp"

#include "hex_dump.hpp"

#include "Game.hpp"

#include <chrono>
#include <stdexcept>
#include <iostream>
#include <cassert>
#include <unordered_map>

#ifdef _WIN32
extern "C"
{
	uint32_t GetACP();
}
#endif
int main(int argc, char **argv)
{
#ifdef _WIN32
	{ // when compiled on windows, check that code page is forced to utf-8 (makes file loading/saving work right):
		// see: https://docs.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page
		uint32_t code_page = GetACP();
		if (code_page == 65001)
		{
			std::cout << "Code page is properly set to UTF-8." << std::endl;
		}
		else
		{
			std::cout << "WARNING: code page is set to " << code_page << " instead of 65001 (UTF-8). Some file handling functions may fail." << std::endl;
		}
	}

	// when compiled on windows, unhandled exceptions don't have their message printed, which can make debugging simple issues difficult.
	try
	{
#endif

		//------------ argument parsing ------------

		if (argc != 2)
		{
			std::cerr << "Usage:\n\t./server <port>" << std::endl;
			return 1;
		}

		//------------ initialization ------------

		Server server(argv[1]);

		//------------ main loop ------------

		// keep track of which connection is controlling which player:
		std::unordered_map<Connection *, Player *> connection_to_player;
		// keep track of game state:
		Game game;

		while (true)
		{
			static auto next_tick = std::chrono::steady_clock::now() + std::chrono::duration<double>(Game::Tick);
			// process incoming data from clients until a tick has elapsed:
			while (true)
			{
				auto now = std::chrono::steady_clock::now();
				double remain = std::chrono::duration<double>(next_tick - now).count();
				if (remain < 0.0)
				{
					next_tick += std::chrono::duration<double>(Game::Tick);
					break;
				}

				// helper used on client close (due to quit) and server close (due to error):
				auto remove_connection = [&](Connection *c)
				{
					auto f = connection_to_player.find(c);
					assert(f != connection_to_player.end());
					game.remove_player(f->second);
					connection_to_player.erase(f);
				};

				server.poll([&](Connection *c, Connection::Event evt)
							{
				if (evt == Connection::OnOpen) {
					//client connected:

					//create some player info for them:
					connection_to_player.emplace(c, game.spawn_player());

				} else if (evt == Connection::OnClose) {
					//client disconnected:

					remove_connection(c);

				} else { assert(evt == Connection::OnRecv);
					//got data from client:
					//std::cout << "current buffer:\n" << hex_dump(c->recv_buffer); std::cout.flush(); //DEBUG

					//look up in players list:
					auto f = connection_to_player.find(c);
					assert(f != connection_to_player.end());
					Player &player = *f->second;

					//handle messages from client:
					try {
						bool handled_message;
						do {
							handled_message = false;
							if (player.controls.recv_controls_message(c)) handled_message = true;
							//TODO: extend for more message types as needed

							Role chosen;
							uint8_t selected;
							if (Game::recv_selected_role_message(c, &selected))
							{
								handled_message = true;

								// who sent this?
								int idx = (&player == &game.players.front()) ? 1 : 2;

								if (idx == 1)
									game.selected_role_1 = selected;
								else
									game.selected_role_2 = selected;

								// NOTE: per your spec, we do NOT touch role_1/role_2 here.
								// Conflict resolution stays in the login (Enter) path.
							}

							if (Game::recv_login_message(c, &chosen))
							{
								handled_message = true;
								player.role = chosen; // add `Role role = Role::Unknown;` to your Player
								// Check role selection logic
								
								int cur_player_idx = (&player == &game.players.front()) ? 1 : 2;

								auto set_selected_opposite = [](uint8_t &selected, Role chosen)
								{
									// chosen: 1=Communicator -> other should *select* Operative (1)
									//         2=Operative    -> other should *select* Communicator (0)
									selected = (chosen == Role::Communicator ? 1 : 0);
									printf("chosen: %d, other player forced to select %s\n", uint8_t(chosen), (selected == 0 ? "Communicator" : "Operative"));
								};

								// write role_1/role_2 from the chosen enum value:
								uint8_t my_selected_role = (chosen == Role::Communicator ? 0 : 1);
								if (cur_player_idx == 1)
								{
									game.role_1 = uint8_t(chosen);
									game.selected_role_1 = my_selected_role;
								}
								else
								{
									game.role_2 = uint8_t(chosen);
									game.selected_role_2 = my_selected_role;
								}

								// figure out the other side:
								uint8_t &other_role = (cur_player_idx == 1 ? game.role_2 : game.role_1);
								uint8_t &other_selected = (cur_player_idx == 1 ? game.selected_role_2 : game.selected_role_1);
								printf("\nother_role = %d, other_selected = %d, chosen = %d, other_role == uint8_t(chosen)? %d\n", other_role, other_selected, uint8_t(chosen), other_role == uint8_t(chosen));

								if (other_role == 0)
								{
									// unknown: wait for the other player
									// (labels are derived client-side)
								}
								else if (other_role == uint8_t(chosen))
								{
									// same role: reset other to unknown, push their selection to the opposite:
									other_role = 0;
									set_selected_opposite(other_selected, chosen);
								}
								else
								{
									// complementary: both ready -> proceed to Communication
									game.phase = Game::Phase::Communication;
								}
							}

							std::string typed;
							if (Game::recv_instruction_message(c, &typed))
							{
								handled_message = true;
								// Move to Operation phase and store the message:
								game.found_count = 0;
								game.attempt_count = 25;
								game.instruction_text = typed;

								// corrupt instruction: replace 50% of non-space chars with '*'
								std::string corrupted = typed;
								std::vector<size_t> nonspace;
								nonspace.reserve(corrupted.size());
								for (size_t i = 0; i < corrupted.size(); ++i)
								{
									if (corrupted[i] != ' ')
										nonspace.push_back(i);
								}
								size_t replace_n = nonspace.size() / 2; // 50%
								std::mt19937 rng{std::random_device{}()};
								std::shuffle(nonspace.begin(), nonspace.end(), rng);
								for (size_t k = 0; k < replace_n; ++k)
								{
									corrupted[nonspace[k]] = '*';
								}
								game.corrupted_instruction = std::move(corrupted);

								game.phase = Game::Phase::Operation;
							}
						} while (handled_message);
					} catch (std::exception const &e) {
						std::cout << "Disconnecting client:" << e.what() << std::endl;
						c->close();
						remove_connection(c);
					}
				} }, remain);
			}

			// update current game state
			game.update(Game::Tick);

			// send updated game state to all clients
			for (auto &[c, player] : connection_to_player)
			{
				game.send_state_message(c, player);
			}
		}

		return 0;

#ifdef _WIN32
	}
	catch (std::exception const &e)
	{
		std::cerr << "Unhandled exception:\n"
				  << e.what() << std::endl;
		return 1;
	}
	catch (...)
	{
		std::cerr << "Unhandled exception (unknown type)." << std::endl;
		throw;
	}
#endif
}

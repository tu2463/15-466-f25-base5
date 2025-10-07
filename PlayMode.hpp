#include "Mode.hpp"
#include "Scene.hpp"

#include "Connection.hpp"
#include "Game.hpp"

#include "FontFT.hpp"
#include "TextHB.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>

struct PlayMode : Mode
{
	PlayMode(Client &client, SDL_Window *window);
	virtual ~PlayMode();

	// functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	// input tracking for local player:
	Player::Controls controls;

	// latest game state (from server):
	Game game;

	// last message from server:
	std::string server_message;

	// connection to server:
	Client &client;

	Role my_role = Role::Unknown;

	// --- Scene ---
	Scene scene; // a local copy (constructed from the loaded scene)
	Scene::Camera *camera = nullptr;
	Scene::Transform *adult_001 = nullptr;

	// --- Lobby phase ---
	void send_login();

	// --- Font --- // Credit: font related code are largely copied from last game
	std::unique_ptr<FontFT> ft;
	std::unique_ptr<TextHB> hb;

	GLuint text_prog = 0;
	GLint text_uColor = -1, text_uTex = -1, text_uClip = -1;
	GLuint text_vao = 0, text_vbo = 0;

	void draw_shaped_text(
		const std::string &s,
		glm::vec3 const &anchor_in,
		glm::vec3 const &x, glm::vec3 const &y,
		glm::u8vec4 const &color,
		glm::mat4 const &world_to_clip);

	// --- Text input --- // Credit: ChatGPT helped me
	SDL_Window *sdl_window = nullptr;
	bool text_input_active = false;
	std::string input_text; // what user typed (UTF-8)
	static constexpr size_t kMaxChars = 150;
	float caret_time = 0.0f; // for blinking caret

private:
	void ensure_text_input_state();
};

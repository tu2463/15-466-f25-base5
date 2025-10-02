#include "PlayMode.hpp"
#include "Scene.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"
#include "hex_dump.hpp"

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include <random>
#include <array>

// Global (file-scope) resources for the room scene:
GLuint room_meshes_for_lit_color_texture_program = 0;

Load<MeshBuffer> room_meshes(LoadTagDefault, []() -> MeshBuffer const *
							 {
    auto *ret = new MeshBuffer(data_path("room.pnct"));
    room_meshes_for_lit_color_texture_program =
        ret->make_vao_for_program(lit_color_texture_program->program);
    return ret; });

Load<Scene> room_scene(LoadTagDefault, []() -> Scene const *
					   { return new Scene(data_path("room.scene"), [&](Scene &scene, Scene::Transform *xf, std::string const &mesh_name)
										  {
        Mesh const &mesh = room_meshes->lookup(mesh_name);
        scene.drawables.emplace_back(xf);
        Scene::Drawable &dr = scene.drawables.back();
        dr.pipeline = lit_color_texture_program_pipeline;
        dr.pipeline.vao   = room_meshes_for_lit_color_texture_program;
        dr.pipeline.type  = mesh.type;
        dr.pipeline.start = mesh.start;
        dr.pipeline.count = mesh.count; }); });

PlayMode::PlayMode(Client &client_) : client(client_), scene(*room_scene)
{
	if (scene.cameras.size() != 1)
	{
		throw std::runtime_error("Expecting 1 camera in room.scene, found " + std::to_string(scene.cameras.size()));
	}
	camera = &scene.cameras.front();
}

PlayMode::~PlayMode()
{
}

void PlayMode::send_login()
{
	Role selected_role = (start_selected == 0) ? Role::Communicator : Role::Operative;
	Game::send_login_message(&client.connection, selected_role);
	// NOTE: data is actually pushed over the wire in client.poll() during update()
	my_role = selected_role;
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size)
{

	if (game.phase == Game::Phase::Lobby)
	{
		if (evt.type == SDL_EVENT_KEY_DOWN && !evt.key.repeat)
		{
			if (evt.key.key == SDLK_W || evt.key.key == SDLK_UP)
			{
				start_selected = (start_selected + 1) % 2; // toggle
				return true;
			}
			else if (evt.key.key == SDLK_S || evt.key.key == SDLK_DOWN)
			{
				start_selected = (start_selected + 1) % 2; // toggle
				return true;
			}
			else if (evt.key.key == SDLK_RETURN || evt.key.key == SDLK_KP_ENTER)
			{
				send_login();
				return true;
			}
		}
		return false;
	}

	// if (evt.type == SDL_EVENT_KEY_DOWN) {
	// 	if (evt.key.repeat) {
	// 		//ignore repeats
	// 	} else if (evt.key.key == SDLK_A) {
	// 		controls.left.downs += 1;
	// 		controls.left.pressed = true;
	// 		return true;
	// 	} else if (evt.key.key == SDLK_D) {
	// 		controls.right.downs += 1;
	// 		controls.right.pressed = true;
	// 		return true;
	// 	} else if (evt.key.key == SDLK_W) {
	// 		controls.up.downs += 1;
	// 		controls.up.pressed = true;
	// 		return true;
	// 	} else if (evt.key.key == SDLK_S) {
	// 		controls.down.downs += 1;
	// 		controls.down.pressed = true;
	// 		return true;
	// 	} else if (evt.key.key == SDLK_SPACE) {
	// 		controls.jump.downs += 1;
	// 		controls.jump.pressed = true;
	// 		return true;
	// 	}
	// } else if (evt.type == SDL_EVENT_KEY_UP) {
	// 	if (evt.key.key == SDLK_A) {
	// 		controls.left.pressed = false;
	// 		return true;
	// 	} else if (evt.key.key == SDLK_D) {
	// 		controls.right.pressed = false;
	// 		return true;
	// 	} else if (evt.key.key == SDLK_W) {
	// 		controls.up.pressed = false;
	// 		return true;
	// 	} else if (evt.key.key == SDLK_S) {
	// 		controls.down.pressed = false;
	// 		return true;
	// 	} else if (evt.key.key == SDLK_SPACE) {
	// 		controls.jump.pressed = false;
	// 		return true;
	// 	}
	// }

	return false;
}

void PlayMode::update(float elapsed)
{

	// queue data for sending to server:
	controls.send_controls_message(&client.connection);

	// reset button press counters:
	controls.left.downs = 0;
	controls.right.downs = 0;
	controls.up.downs = 0;
	controls.down.downs = 0;
	controls.jump.downs = 0;

	// send/receive data:
	client.poll([this](Connection *c, Connection::Event event)
				{
		if (event == Connection::OnOpen) {
			std::cout << "[" << c->socket << "] opened" << std::endl;
		} else if (event == Connection::OnClose) {
			std::cout << "[" << c->socket << "] closed (!)" << std::endl;
			throw std::runtime_error("Lost connection to server!");
		} else { assert(event == Connection::OnRecv);
			//std::cout << "[" << c->socket << "] recv'd data. Current buffer:\n" << hex_dump(c->recv_buffer); std::cout.flush(); //DEBUG
			bool handled_message;
			try {
				do {
					handled_message = false;
					if (game.recv_state_message(c)) handled_message = true;
				} while (handled_message);
			} catch (std::exception const &e) {
				std::cerr << "[" << c->socket << "] malformed message from server: " << e.what() << std::endl;
				//quit the game:
				throw e;
			}
		} }, 0.0);
}

void PlayMode::draw(glm::uvec2 const &drawable_size)
{

	// static std::array<glm::vec2, 16> const circle = []()
	// {
	// 	std::array<glm::vec2, 16> ret;
	// 	for (uint32_t a = 0; a < ret.size(); ++a)
	// 	{
	// 		float ang = a / float(ret.size()) * 2.0f * float(M_PI);
	// 		ret[a] = glm::vec2(std::cos(ang), std::sin(ang));
	// 	}
	// 	return ret;
	// }();

	glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);

	// figure out view transform to center the arena:
	float aspect = float(drawable_size.x) / float(drawable_size.y);
	float scale = std::min(
		2.0f * aspect / (Game::ArenaMax.x - Game::ArenaMin.x + 2.0f * Game::PlayerRadius),
		2.0f / (Game::ArenaMax.y - Game::ArenaMin.y + 2.0f * Game::PlayerRadius));
	glm::vec2 offset = -0.5f * (Game::ArenaMax + Game::ArenaMin);

	glm::mat4 world_to_clip = glm::mat4(
		scale / aspect, 0.0f, 0.0f, offset.x,
		0.0f, scale, 0.0f, offset.y,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);

	{
		DrawLines lines(world_to_clip);

		// helper:
		auto draw_text = [&](glm::vec2 const &at, std::string const &text, float H)
		{
			lines.draw_text(text,
							glm::vec3(at.x, at.y, 0.0),
							glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
							glm::u8vec4(0x00, 0x00, 0x00, 0x00));
			float ofs = (1.0f / scale) / drawable_size.y;
			lines.draw_text(text,
							glm::vec3(at.x + ofs, at.y + ofs, 0.0),
							glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
							glm::u8vec4(0xff, 0xff, 0xff, 0x00));
		};

		float MARGIN_TOP = 0.8f;
		float LINE_SPACING = 0.12f;
		float FONT_H = 0.09f;
		float MARGIN_LEFT = -0.97f;

		if (game.phase == Game::Phase::Lobby)
		{
			// intro text (from server)
			draw_text(glm::vec2(MARGIN_LEFT, MARGIN_TOP), "Welcome back to the terminal, comrade. All current intelligence points to a restaurant. ", FONT_H * 0.6f);
			draw_text(glm::vec2(MARGIN_LEFT, MARGIN_TOP - LINE_SPACING), "It may conceal clues vital to your next move. You and your team must investigate immediately. ", FONT_H * 0.6f);
			draw_text(glm::vec2(MARGIN_LEFT, MARGIN_TOP - LINE_SPACING * 2), "Time is of the essence.", FONT_H * 0.6f);

			draw_text(glm::vec2(MARGIN_LEFT, MARGIN_TOP - LINE_SPACING * 4), "Enter your identity for further instruction:", FONT_H * 0.6f);

			float INTRO_H = MARGIN_TOP - LINE_SPACING * 6;

			auto draw_option = [&](char const *label, int idx)
			{
				bool is_selected = (start_selected == idx);
				std::string s = std::string(is_selected ? "> " : "  ") + label + (is_selected ? " <" : "");
				draw_text(glm::vec2(MARGIN_LEFT + 0.05f, INTRO_H - LINE_SPACING * idx), s, FONT_H);
			};
			draw_option("Communicator", 0);
			draw_option("Operative", 1);

			draw_text(glm::vec2(MARGIN_LEFT + 0.05f, INTRO_H - LINE_SPACING * 3), "[Enter] Log In", FONT_H);
			return;
		}

		if(game.phase == Game::Phase::Communication)
		{
			// 3D scene pass:
			camera->aspect = float(drawable_size.x) / float(drawable_size.y);

			// simple directional light (same as your previous PlayMode)
			glUseProgram(lit_color_texture_program->program);
			glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
			glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f, -1.0f)));
			glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
			glUseProgram(0);

			glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
			glClearDepth(1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LESS);

			scene.draw(*camera);

			glDisable(GL_DEPTH_TEST);

			// 2D overlay UI after the scene:
			float aspect = float(drawable_size.x) / float(drawable_size.y);
			DrawLines lines(glm::mat4(
				1.0f / aspect, 0, 0, 0,
				0, 1.0f, 0, 0,
				0, 0, 1, 0,
				0, 0, 0, 1));

			auto label = [&](glm::vec2 at, std::string const &s, float H)
			{
				glm::vec3 X(H, 0, 0), Y(0, H, 0);
				glm::u8vec4 sh(0, 0, 0, 0xff), fg(0xff, 0xff, 0xff, 0xff);
				float ofs = 2.0f / drawable_size.y;
				lines.draw_text(s, glm::vec3(at, 0.0f), X, Y, sh);
				lines.draw_text(s, glm::vec3(at.x + ofs, at.y + ofs, 0.0f), X, Y, fg);
			};

			// role-dependent text:
			float y0 = +0.9f; // top-left in NDC
			float dy = 0.08f;
			float x0 = -0.95f;
			float H = 0.06f;

			if (my_role == Role::Communicator)
			{
				// label({x0, y0}, "目标已标记为红色。出于保密考虑，你的队友不会得到此情报，只有你掌握线索。", H);
				// label({x0, y0 - dy}, "你必须在有限字符内向队友传递指令，确保队友能够识别目标物体。", H);
				// label({x0, y0 - 2 * dy}, "注意：因技术限制，传输过程中将有一半讯息遭到损毁。请谨慎输入。", H);
				label({x0, y0 - 4 * dy}, "The target objects have been marked in red. For security reasons, your teammate will not receive this information.", H * 0.9f);
				label({x0, y0 - 5 * dy}, "Only you hold the clue.", H * 0.9f);
				label({x0, y0 - 6 * dy}, "You must send instructions to your teammate within a strict character limit, ensuring your teammate can identify the targets.", H * 0.9f);
				label({x0, y0 - 7 * dy}, "Warning: Half the message will be lost in transmission. Proceed with caution.", H * 0.9f);

				// simple "Send" button label (visual only for now)
				label({x0, y0 - 9 * dy}, "[ Send ]", H);
			}
			else if (my_role == Role::Operative)
			{
				// label({x0, y0}, "联络员即将向你发送包含目标物体信息的指令。因技术限制，传输过程中约有一半讯息会被破坏。", H);
				// label({x0, y0 - dy}, "你将收到残缺指令，并解码其内容，随后前往餐厅寻找目标物体。", H);
				// label({x0, y0 - 2 * dy}, "行动须谨慎。多次误判或遗漏等同于任务失败。", H);
				label({x0, y0 - 4 * dy}, "The communicator will soon send you instructions containing details of the target objects.", H * 0.9f);
				label({x0, y0 - 5 * dy}, "Due to technical constraints, roughly half of the message will be lost in transit.", H * 0.9f);
				label({x0, y0 - 6 * dy}, "You will receive a corrupted instruction, decode its contents, and locate the targets at the restaurant.", H * 0.9f);
				label({x0, y0 - 7 * dy}, "Act with caution. Repeated errors or omissions will be treated as mission failure.", H * 0.9f);
			}

			GL_ERRORS();
			return;
		}

		// draw_text(glm::vec2(-0.1f, 0.0f), "start", FONT_H);

		// lines.draw(glm::vec3(Game::ArenaMin.x, Game::ArenaMin.y, 0.0f), glm::vec3(Game::ArenaMax.x, Game::ArenaMin.y, 0.0f), glm::u8vec4(0xff, 0x00, 0xff, 0xff));
		// lines.draw(glm::vec3(Game::ArenaMin.x, Game::ArenaMax.y, 0.0f), glm::vec3(Game::ArenaMax.x, Game::ArenaMax.y, 0.0f), glm::u8vec4(0xff, 0x00, 0xff, 0xff));
		// lines.draw(glm::vec3(Game::ArenaMin.x, Game::ArenaMin.y, 0.0f), glm::vec3(Game::ArenaMin.x, Game::ArenaMax.y, 0.0f), glm::u8vec4(0xff, 0x00, 0xff, 0xff));
		// lines.draw(glm::vec3(Game::ArenaMax.x, Game::ArenaMin.y, 0.0f), glm::vec3(Game::ArenaMax.x, Game::ArenaMax.y, 0.0f), glm::u8vec4(0xff, 0x00, 0xff, 0xff));
		return;
	}
	GL_ERRORS();
}

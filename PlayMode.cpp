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
#include "gl_compile_program.hpp"

#include <random>
#include <array>
#include <unordered_map>

static std::unordered_map<std::string, std::pair<glm::vec3, glm::vec3>> g_bounds_by_transform_name;

static bool screen_to_world_ray(
	glm::uvec2 drawable_size,
	glm::vec2 mouse_px,
	Scene::Camera const &cam,
	glm::mat4 const &cam_world_matrix,
	glm::vec3 const &cam_world,
	glm::vec3 *out_origin,
	glm::vec3 *out_dir)
{
	if (!out_origin || !out_dir)
		return false;

	float nx = (2.0f * (mouse_px.x / float(drawable_size.x))) - 1.0f;
	float ny = -(2.0f * (mouse_px.y / float(drawable_size.y))) + 1.0f;

	glm::vec4 p_near(nx, ny, -1.0f, 1.0f);
	glm::vec4 p_far(nx, ny, 1.0f, 1.0f);

	glm::mat4 P = cam.make_projection();
	glm::mat4 V = glm::inverse(cam_world_matrix);
	glm::mat4 invVP = glm::inverse(P * V);

	// glm::vec4 w_near = invVP * p_near;
	// w_near /= w_near.w;
	// glm::vec4 w_far = invVP * p_far;
	// w_far /= w_far.w;

	glm::vec4 w_far = invVP * p_far;
	w_far /= w_far.w;
	*out_origin = cam_world;
	*out_dir = glm::normalize(glm::vec3(w_far) - cam_world);

	glm::vec3 cam_yforward_world = glm::normalize(glm::vec3(cam_world_matrix * glm::vec4(0, 0, -1, 0)));
	printf("out_dir(%.2f,%.2f,%.2f), cam_yforward_world(%.2f,%.2f,%.2f), dot=%.2f\n",
		   out_dir->x, out_dir->y, out_dir->z,
		   cam_yforward_world.x, cam_yforward_world.y, cam_yforward_world.z,
		   glm::dot(*out_dir, cam_yforward_world));
	if (glm::dot(*out_dir, cam_yforward_world) < 0.0f)
	{
		*out_dir = -*out_dir;
	}
	return true;
}

static bool ray_aabb_intersect(glm::vec3 ray_origin, glm::vec3 ray_dir, glm::vec3 bmin, glm::vec3 bmax, float *t_hit)
{
	float tmin = 0.0f, tmax = 1e30f;
	const float eps = 1e-8f;
	for (int i = 0; i < 3; ++i)
	{
		// float t0 = (bmin[i] - ray_origin[i]) / ray_dir[i];
		// float t1 = (bmax[i] - ray_origin[i]) / ray_dir[i];
		// if (ray_dir[i] < 0.0f)
		// 	std::swap(t0, t1);

		if (std::abs(ray_dir[i]) < eps)
		{
			// Ray parallel to this slab: must already be inside it
			if (ray_origin[i] < bmin[i] || ray_origin[i] > bmax[i])
				return false;
			continue;
		}
		float invD = 1.0f / ray_dir[i];
		float t0 = (bmin[i] - ray_origin[i]) * invD;
		float t1 = (bmax[i] - ray_origin[i]) * invD;
		if (invD < 0.0f)
			std::swap(t0, t1);

		tmin = std::max(t0, tmin);
		tmax = std::min(t1, tmax);
		if (tmax < tmin)
		{
			printf("i: %d, t0=%.2f, t1=%.2f, tmin=%.2f, tmax=%.2f\n", i, t0, t1, tmin, tmax);
			return false;
		}
	}
	if (t_hit)
		*t_hit = tmin;
	return true;
}

GLuint room_meshes_for_lit_color_texture_program = 0;

Load<MeshBuffer> room_meshes(LoadTagDefault, []() -> MeshBuffer const *
							 {
    auto *ret = new MeshBuffer(data_path("room.pnct"));
    room_meshes_for_lit_color_texture_program =
        ret->make_vao_for_program(lit_color_texture_program->program);
    return ret; });

Load<Scene> room_scene(LoadTagDefault, []() -> Scene const *
					   { return new Scene(data_path("room.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name)
										  {
        Mesh const &mesh = room_meshes->lookup(mesh_name);
        scene.drawables.emplace_back(transform);
        Scene::Drawable &dr = scene.drawables.back();
        dr.pipeline = lit_color_texture_program_pipeline;
        dr.pipeline.vao   = room_meshes_for_lit_color_texture_program;
        dr.pipeline.type  = mesh.type;
        dr.pipeline.start = mesh.start;
        dr.pipeline.count = mesh.count;

	 g_bounds_by_transform_name[transform->name] = { mesh.min, mesh.max };
	 printf("g_bounds_by_transform_name[%s] = (%.2f,%.2f,%.2f) to (%.2f,%.2f,%.2f)\n",
		 transform->name.c_str(),
		 mesh.min.x, mesh.min.y, mesh.min.z,
		 mesh.max.x, mesh.max.y, mesh.max.z); }); });

static void utf8_pop_back(std::string &s)
{
	if (s.empty())
		return;
	size_t i = s.size() - 1;
	// move left while on UTF-8 continuation bytes (10xxxxxx):
	while (i > 0 && (uint8_t(s[i]) & 0b11000000) == 0b10000000)
		--i;
	s.erase(i);
}

PlayMode::PlayMode(Client &client_, SDL_Window *window_) : client(client_), scene(*room_scene)
{
	sdl_window = window_;
	if (scene.cameras.size() != 1)
	{
		throw std::runtime_error("Expecting 1 camera in room.scene, found " + std::to_string(scene.cameras.size()));
	}
	camera = &scene.cameras.front();

	for (auto &transform : scene.transforms)
	{
		if (transform.name == "Adult.001")
		{ // exact Blender object name
			adult_001 = &transform;
			break;
		}
	}

	// Credit: used ChatGPT to help me understand and set up text rendering
	// --- Font ---
	// 1) load font (CourierPrime-Bold.ttf) and create HB shaper:
	ft = std::make_unique<FontFT>(data_path("CourierPrime-Bold.ttf"), 48); // fixed px size
	hb = std::make_unique<TextHB>(ft->get_ft_face());					   // HB bound to FT face

	// 2) minimal text shader (pos.xy + uv.xy, GL_R8 texture):
	const char *vs = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;   // WORLD space (x,y); z=0
layout(location=1) in vec2 aUV;
uniform mat4 uClip;
out vec2 vUV;
void main(){
  vUV = aUV;
  gl_Position = uClip * vec4(aPos, 0.0, 1.0);
}
)GLSL";
	const char *fs = R"GLSL(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTex;  // GL_R8 atlas
uniform vec3 uColor;
void main(){
  float a = texture(uTex, vUV).r;
  FragColor = vec4(uColor, a);
}
)GLSL";
	text_prog = gl_compile_program(vs, fs); // same helper you used before :contentReference[oaicite:9]{index=9}
	glUseProgram(text_prog);
	text_uColor = glGetUniformLocation(text_prog, "uColor");
	text_uTex = glGetUniformLocation(text_prog, "uTex");
	text_uClip = glGetUniformLocation(text_prog, "uClip");
	glUniform1i(text_uTex, 0);
	glUseProgram(0);

	// 3) VAO/VBO for one glyph quad:
	glGenVertexArrays(1, &text_vao);
	glGenBuffers(1, &text_vbo);
	glBindVertexArray(text_vao);
	glBindBuffer(GL_ARRAY_BUFFER, text_vbo);
	glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Credit: used ChatGPT to help me understand the opengl functions and calculate the corret position to render text
void PlayMode::draw_shaped_text(
	const std::string &s,
	glm::vec3 const &anchor_in,
	glm::vec3 const &x, glm::vec3 const &y,
	glm::u8vec4 const &color,
	glm::mat4 const &world_to_clip)
{
	if (!hb || !ft || s.empty())
		return;

	auto run = hb->shape(s); // HB shaping result :contentReference[oaicite:11]{index=11}
	if (run.infos.empty())
		return;

	float font_px = float(ft->pixel_size());
	glm::vec3 per_px_x = x / font_px;
	glm::vec3 per_px_y = y / font_px;

	glUseProgram(text_prog);
	glUniform3f(text_uColor, color.r / 255.f, color.g / 255.f, color.b / 255.f);
	glUniformMatrix4fv(text_uClip, 1, GL_FALSE, glm::value_ptr(world_to_clip));
	glActiveTexture(GL_TEXTURE0);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glm::vec3 pen = anchor_in;
	glBindVertexArray(text_vao);
	glBindBuffer(GL_ARRAY_BUFFER, text_vbo);

	for (size_t i = 0; i < run.infos.size(); ++i)
	{
		FT_UInt gi = run.infos[i].codepoint;
		auto const &pos = run.poss[i];
		float x_off = float(pos.x_offset) / 64.f, y_off = float(pos.y_offset) / 64.f;
		float x_adv = float(pos.x_advance) / 64.f, y_adv = float(pos.y_advance) / 64.f;

		auto const &g = ft->get_glyph(gi); // FT-rendered bitmap + GL_R8 tex :contentReference[oaicite:12]{index=12}

		glm::vec3 base = pen + per_px_x * (x_off + g.bearing.x) + per_px_y * (y_off - g.bearing.y);

		glm::vec3 p00 = base;
		glm::vec3 p10 = base + per_px_x * float(g.size.x);
		glm::vec3 p11 = p10 + per_px_y * float(g.size.y);
		glm::vec3 p01 = base + per_px_y * float(g.size.y);

		float verts[6 * 4] = {
			p00.x, p00.y, 0.f, 1.f,
			p10.x, p10.y, 1.f, 1.f,
			p11.x, p11.y, 1.f, 0.f,
			p00.x, p00.y, 0.f, 1.f,
			p11.x, p11.y, 1.f, 0.f,
			p01.x, p01.y, 0.f, 0.f};

		glBindTexture(GL_TEXTURE_2D, g.tex);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
		glDrawArrays(GL_TRIANGLES, 0, 6);

		pen += per_px_x * (x_adv /* + optional tracking */) + per_px_y * y_adv;
	}

	glDisable(GL_BLEND);
	glUseProgram(0);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

PlayMode::~PlayMode()
{
	if (text_input_active && sdl_window)
		SDL_StopTextInput(sdl_window);
}

void PlayMode::send_login()
{
	uint8_t &my_selected_role = (game.self_index == 1) ? game.selected_role_1 : game.selected_role_2;
	Role selected_role = (my_selected_role == 0) ? Role::Communicator : Role::Operative;
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
			// Read the current server-pushed value and compute the toggle client-side:
			uint8_t my_selected_role = (game.self_index == 1) ? game.selected_role_1 : game.selected_role_2;

			if (evt.key.key == SDLK_W || evt.key.key == SDLK_UP)
			{
				uint8_t new_selected = (my_selected_role ^ 1);
				Game::send_selected_role_message(&client.connection, new_selected);
				return true;
			}
			else if (evt.key.key == SDLK_S || evt.key.key == SDLK_DOWN)
			{
				uint8_t new_selected = (my_selected_role ^ 1);
				Game::send_selected_role_message(&client.connection, new_selected);
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

	if (game.phase == Game::Phase::Communication && my_role == Role::Communicator)
	{
		// accept text input events (IME-friendly):
		if (evt.type == SDL_EVENT_TEXT_INPUT && text_input_active)
		{
			// append, enforce char limit (count by Unicode codepoints if you want; for now limit bytes)
			if (input_text.size() + std::strlen(evt.text.text) <= kMaxChars)
			{
				input_text.append(evt.text.text);
			}
			else
			{
				// trim to max
				size_t room = (kMaxChars > input_text.size()) ? (kMaxChars - input_text.size()) : 0;
				input_text.append(evt.text.text, room);
			}
			return true;
		}
		if (evt.type == SDL_EVENT_KEY_DOWN && !evt.key.repeat)
		{
			if (evt.key.key == SDLK_BACKSPACE)
			{
				utf8_pop_back(input_text);
				return true;
			}
			else if (evt.key.key == SDLK_RETURN || evt.key.key == SDLK_KP_ENTER)
			{
				if (!input_text.empty())
				{
					// Don't flip phase locally; server will broadcast Operation+text.
					Game::send_instruction_message(&client.connection, input_text);
				}
				return true;
			}
		}
		// swallow other keys here if you don't want gameplay to trigger
	}

	if (game.phase == Game::Phase::Operation && my_role == Role::Operative)
	{
		// printf("- operative mouse event\n");
		if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
		{
			// printf("-- operative mouse button down\n");
			if (!adult_001)
			{
				printf("no adult_001\n");
				return true;
			}

			// 1) world ray
			glm::vec3 ray_origin, ray_dir;
			glm::vec2 mouse_px = glm::vec2(float(evt.button.x), float(evt.button.y));
			glm::mat4 cam_world_matrix = camera->transform->make_world_from_local();
			printf("cam_world_matrix = [[%.2f,%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f,%.2f]]\n",
				   cam_world_matrix[0][0], cam_world_matrix[0][1], cam_world_matrix[0][2], cam_world_matrix[0][3],
				   cam_world_matrix[1][0], cam_world_matrix[1][1], cam_world_matrix[1][2], cam_world_matrix[1][3],
				   cam_world_matrix[2][0], cam_world_matrix[2][1], cam_world_matrix[2][2], cam_world_matrix[2][3],
				   cam_world_matrix[3][0], cam_world_matrix[3][1], cam_world_matrix[3][2], cam_world_matrix[3][3]);

			glm::vec3 cam_world = glm::vec3(cam_world_matrix * glm::vec4(0, 0, 0, 1));
			printf("cam_world(%.2f,%.2f,%.2f)\n", cam_world.x, cam_world.y, cam_world.z)
			;

			if (!screen_to_world_ray(window_size, mouse_px, *camera, cam_world_matrix, cam_world, &ray_origin, &ray_dir))
			{
				printf("no ray\n");
				return true;
			}
			printf("ray_origin=(%.2f,%.2f,%.2f) ray_dir=(%.2f,%.2f,%.2f)\n",
				   ray_origin.x, ray_origin.y, ray_origin.z,
				   ray_dir.x, ray_dir.y, ray_dir.z);

			// 2) bring ray into object(local) space
			glm::vec3 adult_world = glm::vec3(adult_001->make_world_from_local() * glm::vec4(0, 0, 0, 1)); // DEBUG
			printf("adult_world(%.2f,%.2f,%.2f)\n", adult_world.x, adult_world.y, adult_world.z);

			glm::mat4 adult_local_matrix = adult_001->make_local_from_world();
			glm::vec3 adult_local = glm::vec3(adult_local_matrix * glm::vec4(0, 0, 0, 1));

			printf("adult_local_matrix = [[%.2f,%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f,%.2f],[%.2f,%.2f,%.2f,%.2f]]\n",
				   adult_local_matrix[0][0], adult_local_matrix[0][1], adult_local_matrix[0][2], adult_local_matrix[0][3],
				   adult_local_matrix[1][0], adult_local_matrix[1][1], adult_local_matrix[1][2], adult_local_matrix[1][3],
				   adult_local_matrix[2][0], adult_local_matrix[2][1], adult_local_matrix[2][2], adult_local_matrix[2][3],
				   adult_local_matrix[3][0], adult_local_matrix[3][1], adult_local_matrix[3][2], adult_local_matrix[3][3]);	
				   
			printf("adult_local(%.2f,%.2f,%.2f)\n",
				   adult_local.x, adult_local.y, adult_local.z);

			glm::vec3 ray_origin_local = glm::vec3(adult_local_matrix * glm::vec4(ray_origin, 1.0f));
			glm::vec3 ray_dir_local = glm::normalize(glm::mat3(adult_local_matrix) * ray_dir);
			printf("ray_origin_local(%.2f,%.2f,%.2f), ray_dir_local(%.2f,%.2f,%.2f)\n",
				   ray_origin_local.x, ray_origin_local.y, ray_origin_local.z,
				   ray_dir_local.x, ray_dir_local.y, ray_dir_local.z);

			// 3) local-space AABB
			auto it = g_bounds_by_transform_name.find(adult_001->name);
			if (it == g_bounds_by_transform_name.end())
			{
				printf("no bound. g_bounds_by_transform_name: ");
				for (const auto &it : g_bounds_by_transform_name)
					printf("  %s, ", it.first.c_str());
				printf("\n");
				return true;
			}
			glm::vec3 bmin = it->second.first;
			glm::vec3 bmax = it->second.second;
			printf("bound of adult_001 bmin=(%.2f,%.2f,%.2f) bmax=(%.2f,%.2f,%.2f)\n",
				   bmin.x, bmin.y, bmin.z,
				   bmax.x, bmax.y, bmax.z);

			

			// 4) intersect
			float t;
			if (ray_aabb_intersect(ray_origin_local, ray_dir_local, bmin, bmax, &t))
			{
				printf("hit %s t=%f\n", adult_001->name.c_str(), t);
			}
			else
			{
				printf("miss %s\n", adult_001->name.c_str());
			}
			return true;
		}
	}

	return false;
}

void PlayMode::ensure_text_input_state()
{
	if (my_role == Role::Communicator && !text_input_active)
	{
		if (sdl_window)
			SDL_StartTextInput(sdl_window);
		text_input_active = true;
	}
	else if (my_role != Role::Communicator && text_input_active)
	{
		if (sdl_window)
			SDL_StopTextInput(sdl_window);
		text_input_active = false;
	}
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

	if (game.phase == Game::Phase::Communication)
	{
		caret_time += elapsed;
		ensure_text_input_state();
	}
}

void PlayMode::draw(glm::uvec2 const &drawable_size)
{
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
		float MARGIN_TOP = 0.95f;
		float FONT_H = 0.06f;
		float LINE_SPACING = FONT_H * 1.4f;
		float MARGIN_LEFT = -1.5f;

		// 2D overlay space (NDC-ish), same framing you used:
		float aspect = float(drawable_size.x) / float(drawable_size.y);
		glm::mat4 clip = glm::mat4(
			1.0f / aspect, 0, 0, 0,
			0, 1.0f, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1);

		// baseline sizes (in "world" units): FONT_H controls visual point size
		glm::vec3 X = glm::vec3(FONT_H, 0, 0);
		glm::vec3 Y = glm::vec3(0, FONT_H, 0);

		glm::vec3 DESCRIPTION_X = 0.8f * X;
		glm::vec3 DESCRIPTION_Y = 0.8f * Y;

		glm::u8vec4 DEFAULT_COLOR = {85, 51, 45, 255};
		glm::u8vec4 HIGHLIGHT_COLOR = {237, 120, 64, 255};

		if (game.phase == Game::Phase::Lobby)
		{
			draw_shaped_text("Welcome back to the terminal, comrade. All current intelligence points to a restaurant. ", {MARGIN_LEFT, MARGIN_TOP, 0}, DESCRIPTION_X, DESCRIPTION_Y, DEFAULT_COLOR, clip);
			draw_shaped_text("It may conceal clues vital to our next move. You and your team must investigate immediately. ", {MARGIN_LEFT, MARGIN_TOP - LINE_SPACING, 0}, DESCRIPTION_X, DESCRIPTION_Y, DEFAULT_COLOR, clip);
			draw_shaped_text("Time is of the essence.", {MARGIN_LEFT, MARGIN_TOP - LINE_SPACING * 2, 0}, DESCRIPTION_X, DESCRIPTION_Y, DEFAULT_COLOR, clip);

			uint8_t &my_selected_role = (game.self_index == 1) ? game.selected_role_1 : game.selected_role_2;
			draw_shaped_text("Enter your identity for further instruction:", {MARGIN_LEFT, MARGIN_TOP - LINE_SPACING * 4, 0}, DESCRIPTION_X, DESCRIPTION_Y, DEFAULT_COLOR, clip);

			float INTRO_TOP = MARGIN_TOP - LINE_SPACING * 6;

			std::string option_0 = (my_selected_role == 0 ? "> " : "  ") + std::string("Communicator") + (my_selected_role == 0 ? " <" : "");
			std::string option_1 = (my_selected_role == 1 ? "> " : "  ") + std::string("Operative") + (my_selected_role == 1 ? " <" : "");
			draw_shaped_text(option_0, {MARGIN_LEFT + 0.05f, INTRO_TOP - 2 * LINE_SPACING, 0}, X, Y, {255, 255, 0, 255}, clip);
			draw_shaped_text(option_1, {MARGIN_LEFT + 0.05f, INTRO_TOP - 3 * LINE_SPACING, 0}, X, Y, {255, 255, 0, 255}, clip);

			// label depends on whether this client has chosen a role
			uint8_t my_role = 0; // unknown
			if (game.self_index == 1)
				my_role = game.role_1;
			else if (game.self_index == 2)
				my_role = game.role_2;

			std::string label = (my_role == 0)
									? "[Enter] Log In"
									: "[Enter] Log In; Waiting for your teammate to log in…";

			draw_shaped_text(label, {MARGIN_LEFT + 0.05f, INTRO_TOP - 5 * LINE_SPACING, 0}, X, Y, {200, 200, 200, 255}, clip);
			GL_ERRORS();
			return;
		}

		if (game.phase == Game::Phase::Communication)
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
			DrawLines lines(glm::mat4(
				1.0f / aspect, 0, 0, 0,
				0, 1.0f, 0, 0,
				0, 0, 1, 0,
				0, 0, 0, 1));

			if (my_role == Role::Communicator)
			{
				float MARGIN_TOP = +0.95f;
				float FONT_H = 0.06f;
				float LINE_SPACING = FONT_H * 1.4f;
				float MARGIN_LEFT = -1.75f;

				draw_shaped_text("The target objects have been marked in red. For security reasons, your teammate will not receive this information.", {MARGIN_LEFT, MARGIN_TOP, 0}, DESCRIPTION_X, DESCRIPTION_Y, DEFAULT_COLOR, clip);
				draw_shaped_text("Only you hold the clue.", {MARGIN_LEFT, MARGIN_TOP - 1 * LINE_SPACING, 0}, DESCRIPTION_X, DESCRIPTION_Y, DEFAULT_COLOR, clip);
				draw_shaped_text("You must send instructions to your teammate within a strict character limit,", {MARGIN_LEFT, MARGIN_TOP - 2 * LINE_SPACING, 0}, DESCRIPTION_X, DESCRIPTION_Y, DEFAULT_COLOR, clip);
				draw_shaped_text("ensuring your teammate can identify the targets.", {MARGIN_LEFT, MARGIN_TOP - 3 * LINE_SPACING, 0}, DESCRIPTION_X, DESCRIPTION_Y, DEFAULT_COLOR, clip);
				draw_shaped_text("Warning: Half the message will be lost in transmission.", {MARGIN_LEFT, MARGIN_TOP - 4 * LINE_SPACING, 0}, DESCRIPTION_X, DESCRIPTION_Y, HIGHLIGHT_COLOR, clip);

				float INPUT_TOP = MARGIN_TOP - 6.0f * LINE_SPACING; // a bit under your instructions
				draw_shaped_text("MESSAGE (max 150):", {MARGIN_LEFT, INPUT_TOP, 0}, X, Y, DEFAULT_COLOR, clip);

				// caret blink at 1Hz:
				bool caret_on = (std::fmod(caret_time, 1.0f) < 0.5f);
				std::string view = input_text + (caret_on ? "|" : " ");

				draw_shaped_text(view, {MARGIN_LEFT, INPUT_TOP - LINE_SPACING, 0}, 0.6f * X, 0.6f * Y, HIGHLIGHT_COLOR, clip);
				draw_shaped_text("[Enter] Send", {MARGIN_LEFT, INPUT_TOP - 2 * LINE_SPACING, 0}, X, Y, DEFAULT_COLOR, clip);
			}
			else if (my_role == Role::Operative)
			{
				float MARGIN_TOP = +1.25f;
				float FONT_H = 0.06f;
				float LINE_SPACING = FONT_H * 1.4f;
				float MARGIN_LEFT = -1.75f;

				draw_shaped_text("The communicator will soon send you instructions containing details of the target objects.", {MARGIN_LEFT, MARGIN_TOP - 4 * LINE_SPACING, 0}, DESCRIPTION_X, DESCRIPTION_Y, DEFAULT_COLOR, clip);
				draw_shaped_text("Due to technical constraints, roughly half of the message will be lost in transit.", {MARGIN_LEFT, MARGIN_TOP - 5 * LINE_SPACING, 0}, DESCRIPTION_X, DESCRIPTION_Y, DEFAULT_COLOR, clip);
				draw_shaped_text("You will receive a corrupted instruction, decode its contents, and locate the targets at the restaurant.", {MARGIN_LEFT, MARGIN_TOP - 6 * LINE_SPACING, 0}, DESCRIPTION_X, DESCRIPTION_Y, DEFAULT_COLOR, clip);
				draw_shaped_text("Act with caution. Repeated errors or omissions will be treated as mission failure.", {MARGIN_LEFT, MARGIN_TOP - 7 * LINE_SPACING, 0}, DESCRIPTION_X, DESCRIPTION_Y, {255, 220, 220, 255}, clip);
			}

			GL_ERRORS();
			return;
		}

		if (game.phase == Game::Phase::Operation)
		{
			// 3D scene:
			camera->aspect = float(drawable_size.x) / float(drawable_size.y);

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

			std::string label;
			if (my_role == Role::Communicator)
			{
				label = "Your teammate receives:";
			}
			else
			{
				label = "Instruction reiceived:";
			}
			draw_shaped_text(label, {MARGIN_LEFT, MARGIN_TOP, 0}, DESCRIPTION_X, DESCRIPTION_Y, DEFAULT_COLOR, clip);
			draw_shaped_text(game.corrupted_instruction, {MARGIN_LEFT, MARGIN_TOP - LINE_SPACING, 0}, X, Y, {255, 255, 0, 255}, clip);

			float STATS_TOP = MARGIN_TOP - LINE_SPACING;
			draw_shaped_text(
				"Found: " + std::to_string(game.found_count) + " / 5",
				{MARGIN_LEFT, STATS_TOP, 0}, DESCRIPTION_X, DESCRIPTION_Y, {220, 255, 220, 255}, clip);
			draw_shaped_text(
				"Attempts left: " + std::to_string(game.attempt_count),
				{MARGIN_LEFT, STATS_TOP - LINE_SPACING, 0}, DESCRIPTION_X, DESCRIPTION_Y, {220, 220, 255, 255}, clip);

			GL_ERRORS();
			return;
		}

		// lines.draw(glm::vec3(Game::ArenaMin.x, Game::ArenaMin.y, 0.0f), glm::vec3(Game::ArenaMax.x, Game::ArenaMin.y, 0.0f), glm::u8vec4(0xff, 0x00, 0xff, 0xff));
		// lines.draw(glm::vec3(Game::ArenaMin.x, Game::ArenaMax.y, 0.0f), glm::vec3(Game::ArenaMax.x, Game::ArenaMax.y, 0.0f), glm::u8vec4(0xff, 0x00, 0xff, 0xff));
		// lines.draw(glm::vec3(Game::ArenaMin.x, Game::ArenaMin.y, 0.0f), glm::vec3(Game::ArenaMin.x, Game::ArenaMax.y, 0.0f), glm::u8vec4(0xff, 0x00, 0xff, 0xff));
		// lines.draw(glm::vec3(Game::ArenaMax.x, Game::ArenaMin.y, 0.0f), glm::vec3(Game::ArenaMax.x, Game::ArenaMax.y, 0.0f), glm::u8vec4(0xff, 0x00, 0xff, 0xff));
		return;
	}
	GL_ERRORS();
}

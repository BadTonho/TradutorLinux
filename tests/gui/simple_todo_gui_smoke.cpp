#include <X11/Xlib.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr int kTimeoutMs = 15000;
constexpr int kMenuRowHeight = 24;

// Layout binário do Todo/TodoList no PE32+ MinGW pinado. Esses offsets são
// parte do contrato do artefato que o smoke verifica, não do runtime geral.
constexpr std::size_t kTodoSize = 632;
constexpr std::size_t kTodoPriorityOffset = 604;
constexpr std::size_t kTodoCompletedOffset = 608;
constexpr std::size_t kTodoListCountOffset = kTodoSize * 100;

[[noreturn]] void fail(const std::string& message) {
    std::fprintf(stderr, "simple_todo_gui_smoke: %s\n", message.c_str());
    std::exit(1);
}

[[noreturn]] void skip(const std::string& message) {
    std::fprintf(stderr, "simple_todo_gui_smoke: SKIP: %s\n", message.c_str());
    std::exit(77);
}

void sleep_short() {
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
}

pid_t g_xvfb = -1;

std::string start_xvfb() {
    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) != 0) {
        skip("pipe do Xvfb falhou");
    }
    const pid_t child = ::fork();
    if (child == 0) {
        ::close(pipe_fds[0]);
        if (::dup2(pipe_fds[1], 3) < 0) {
            ::_exit(127);
        }
        ::close(pipe_fds[1]);
        const int null_fd = ::open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            ::dup2(null_fd, STDERR_FILENO);
            ::close(null_fd);
        }
        ::execlp("Xvfb", "Xvfb", "-displayfd", "3", "-screen", "0", "900x700x24",
                 "-nolisten", "tcp", "-ac", static_cast<char*>(nullptr));
        ::_exit(127);
    }
    ::close(pipe_fds[1]);
    if (child < 0) {
        ::close(pipe_fds[0]);
        skip("fork do Xvfb falhou");
    }
    std::string number;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline && number.empty()) {
        struct pollfd descriptor{pipe_fds[0], POLLIN | POLLHUP, 0};
        if (::poll(&descriptor, 1, 250) <= 0) {
            continue;
        }
        char value = '\0';
        const ssize_t count = ::read(pipe_fds[0], &value, 1);
        if (count == 1 && value != '\n') {
            number.push_back(value);
        } else if (count <= 0) {
            break;
        }
    }
    ::close(pipe_fds[0]);
    if (number.empty()) {
        ::kill(child, SIGKILL);
        ::waitpid(child, nullptr, 0);
        skip("Xvfb não iniciou");
    }
    const std::string display = ":" + number;
    for (int attempt = 0; attempt < 50; ++attempt) {
        Display* probe = XOpenDisplay(display.c_str());
        if (probe != nullptr) {
            XCloseDisplay(probe);
            g_xvfb = child;
            return display;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ::kill(child, SIGKILL);
    ::waitpid(child, nullptr, 0);
    skip("display Xvfb não ficou disponível");
}

Window find_named_window(Display* display, const char* caption) noexcept {
    Window root_return = 0;
    Window parent_return = 0;
    Window* children = nullptr;
    unsigned int count = 0;
    if (XQueryTree(display, RootWindow(display, DefaultScreen(display)), &root_return,
                   &parent_return, &children, &count) == 0) {
        return 0;
    }
    Window result = 0;
    for (unsigned int index = 0; index < count && result == 0; ++index) {
        char* name = nullptr;
        if (XFetchName(display, children[index], &name) != 0 && name != nullptr &&
            std::strcmp(name, caption) == 0) {
            result = children[index];
        }
        if (name != nullptr) {
            XFree(name);
        }
    }
    if (children != nullptr) {
        XFree(children);
    }
    return result;
}

Window wait_for_window(const std::string& display, const char* caption) {
    Display* dpy = XOpenDisplay(display.c_str());
    if (dpy == nullptr) {
        fail("não foi possível abrir display do teste");
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kTimeoutMs);
    Window result = 0;
    while (std::chrono::steady_clock::now() < deadline && result == 0) {
        result = find_named_window(dpy, caption);
        if (result == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    XCloseDisplay(dpy);
    if (result == 0) {
        fail(std::string{"janela não encontrada: "} + caption);
    }
    return result;
}

void send_button(const std::string& display_name, const Window window, const int x, const int y,
                 const unsigned int button = 1) {
    Display* dpy = XOpenDisplay(display_name.c_str());
    if (dpy == nullptr) {
        fail("display indisponível ao enviar mouse");
    }
    const Window root = RootWindow(dpy, DefaultScreen(dpy));
    for (int type : {ButtonPress, ButtonRelease}) {
        XEvent event{};
        event.xbutton.type = type;
        event.xbutton.display = dpy;
        event.xbutton.window = window;
        event.xbutton.root = root;
        event.xbutton.time = CurrentTime;
        event.xbutton.x = x;
        event.xbutton.y = y;
        event.xbutton.x_root = x;
        event.xbutton.y_root = y;
        event.xbutton.button = button;
        event.xbutton.same_screen = True;
        XSendEvent(dpy, window, False, type == ButtonPress ? ButtonPressMask : ButtonReleaseMask,
                   &event);
    }
    XFlush(dpy);
    XCloseDisplay(dpy);
}

void send_right_click(const std::string& display_name, const Window window, const int x, const int y) {
    send_button(display_name, window, x, y, 3);
}

void send_key(const std::string& display_name, const Window window, const KeySym keysym,
              const unsigned int state = 0) {
    Display* dpy = XOpenDisplay(display_name.c_str());
    if (dpy == nullptr) {
        fail("display indisponível ao enviar teclado");
    }
    const KeyCode code = XKeysymToKeycode(dpy, keysym);
    if (code == 0) {
        XCloseDisplay(dpy);
        fail("keysym sem keycode no Xvfb");
    }
    const Window root = RootWindow(dpy, DefaultScreen(dpy));
    for (int type : {KeyPress, KeyRelease}) {
        XEvent event{};
        event.xkey.type = type;
        event.xkey.display = dpy;
        event.xkey.window = window;
        event.xkey.root = root;
        event.xkey.time = CurrentTime;
        event.xkey.x = 20;
        event.xkey.y = 20;
        event.xkey.x_root = 20;
        event.xkey.y_root = 20;
        event.xkey.state = state;
        event.xkey.keycode = code;
        event.xkey.same_screen = True;
        XSendEvent(dpy, window, False, type == KeyPress ? KeyPressMask : KeyReleaseMask, &event);
    }
    XFlush(dpy);
    XCloseDisplay(dpy);
}

void type_text(const std::string& display_name, const Window window, const std::string& text) {
    for (const char character : text) {
        if (character == ' ') {
            send_key(display_name, window, XStringToKeysym("space"));
        } else {
            const bool upper = character >= 'A' && character <= 'Z';
            const char lower = upper ? static_cast<char>(character - 'A' + 'a') : character;
            char name[2] = {lower, '\0'};
            send_key(display_name, window, XStringToKeysym(name), upper ? ShiftMask : 0);
        }
        sleep_short();
    }
}

void erase_text(const std::string& display_name, const Window window, const int count) {
    for (int index = 0; index < count; ++index) {
        send_key(display_name, window, XStringToKeysym("BackSpace"));
        sleep_short();
    }
}

Window wait_for_popup(const std::string& display_name) {
    return wait_for_window(display_name, "TradutorLinuxPopup");
}

void choose_menu(const std::string& display_name, const std::size_t row) {
    const Window popup = wait_for_popup(display_name);
    send_button(display_name, popup, 20, static_cast<int>(row) * kMenuRowHeight + 12);
    sleep_short();
}

pid_t start_guest(const std::string& runtime, const std::string& executable,
                  const std::string& display, const std::filesystem::path& work) {
    const int stdout_fd = ::open((work / "stdout.log").c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
    const int stderr_fd = ::open((work / "trace.log").c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (stdout_fd < 0 || stderr_fd < 0) {
        fail("não foi possível criar logs do alvo");
    }
    const pid_t child = ::fork();
    if (child == 0) {
        ::dup2(stdout_fd, STDOUT_FILENO);
        ::dup2(stderr_fd, STDERR_FILENO);
        ::close(stdout_fd);
        ::close(stderr_fd);
        if (::chdir(work.c_str()) != 0) {
            ::_exit(127);
        }
        ::setenv("DISPLAY", display.c_str(), 1);
        ::setenv("APPDATA", "appdata", 1);
        ::execl(runtime.c_str(), runtime.c_str(), "--trace", executable.c_str(),
                static_cast<char*>(nullptr));
        ::_exit(127);
    }
    ::close(stdout_fd);
    ::close(stderr_fd);
    if (child < 0) {
        fail("fork do runtime falhou");
    }
    return child;
}

int wait_guest(const pid_t child) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kTimeoutMs);
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        if (::waitpid(child, &status, WNOHANG) == child) {
            return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ::kill(child, SIGKILL);
    ::waitpid(child, nullptr, 0);
    fail("runtime não encerrou pelo menu Exit");
}

void require_file(const std::filesystem::path& path, const char* description) {
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) == 0) {
        fail(std::string{"artefato ausente ou vazio: "} + description);
    }
}

std::string read_binary(const std::filesystem::path& path, const char* description) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        fail(std::string{"não foi possível ler: "} + description);
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void require_binary_contains(const std::filesystem::path& path, const char* value,
                             const char* description) {
    const std::string contents = read_binary(path, description);
    if (contents.find(value) == std::string::npos) {
        fail(std::string{"conteúdo ausente em "} + description + ": " + value);
    }
}

void require_todo_state(const std::filesystem::path& path, const std::int32_t count,
                        const std::int32_t priority, const std::int32_t completed) {
    const std::string contents = read_binary(path, "todos.dat");
    if (contents.size() < kTodoListCountOffset + sizeof(std::int32_t)) {
        fail("todos.dat menor que o layout esperado");
    }
    std::int32_t stored_count = 0;
    std::memcpy(&stored_count, contents.data() + kTodoListCountOffset, sizeof(stored_count));
    if (stored_count != count) {
        fail("quantidade persistida de tarefas inesperada");
    }
    if (count == 0) {
        return;
    }
    std::int32_t stored_priority = 0;
    std::int32_t stored_completed = 0;
    std::memcpy(&stored_priority, contents.data() + kTodoPriorityOffset, sizeof(stored_priority));
    std::memcpy(&stored_completed, contents.data() + kTodoCompletedOffset, sizeof(stored_completed));
    if (stored_priority != priority || stored_completed != completed) {
        fail("estado persistido de prioridade/conclusão inesperado");
    }
}

void require_registry_state(const std::filesystem::path& path, const bool enabled) {
    if (!std::filesystem::is_regular_file(path)) {
        fail("artefato ausente: registro de autorun");
    }
    std::ifstream input(path);
    std::string value;
    std::getline(input, value);
    if (enabled == value.empty()) {
        fail(enabled ? "autorun não foi persistido" : "autorun não foi removido");
    }
}

void require_run_artifacts(const std::filesystem::path& work) {
    std::ifstream stdout_file(work / "stdout.log");
    const std::string stdout_contents((std::istreambuf_iterator<char>(stdout_file)),
                                      std::istreambuf_iterator<char>());
    if (!stdout_contents.empty()) {
        fail("stdout do aplicativo não está vazio");
    }
    std::ifstream trace_file(work / "trace.log");
    const std::string trace((std::istreambuf_iterator<char>(trace_file)),
                            std::istreambuf_iterator<char>());
    for (const char* needle : {"RegisterClassExA", "CreateWindowExA", "GetMessageA",
                               "ExitProcess"}) {
        if (trace.find(needle) == std::string::npos) {
            fail(std::string{"evento ausente no trace: "} + needle);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        fail("uso: simple_todo_gui_smoke <runtime> <simple_todo.exe> <workdir>");
    }
    const std::string runtime = argv[1];
    const std::string executable = argv[2];
    const std::filesystem::path work = argv[3];
    std::filesystem::remove_all(work);
    std::filesystem::create_directories(work / "appdata");
    const std::string display = start_xvfb();
    const pid_t child = start_guest(runtime, executable, display, work);
    const Window main_window = wait_for_window(display, "Todo Application");

    // Adicionar tarefa, incluindo descrição e prioridade 3.
    send_button(display, main_window, 40, 390);
    type_text(display, main_window, "Task alpha");
    send_button(display, main_window, 175, 390);
    type_text(display, main_window, "Description beta");
    send_button(display, main_window, 480, 390);
    send_button(display, main_window, 480, 390);
    send_button(display, main_window, 310, 390);
    sleep_short();
    const std::filesystem::path todo_file = work / "appdata/TodoApp/todos.dat";
    require_file(todo_file, "todos.dat após adicionar");
    require_binary_contains(todo_file, "Task alpha", "todos.dat após adicionar");
    require_binary_contains(todo_file, "Description beta", "todos.dat após adicionar");
    require_todo_state(todo_file, 1, 3, 0);

    // Editar título, descrição e prioridade.
    send_button(display, main_window, 100, 80);
    send_button(display, main_window, 50, 428);
    erase_text(display, main_window, 64);
    type_text(display, main_window, "Task edited");
    send_button(display, main_window, 175, 390);
    erase_text(display, main_window, 64);
    type_text(display, main_window, "Description edited");
    send_button(display, main_window, 480, 390);
    send_button(display, main_window, 140, 428);
    sleep_short();
    require_binary_contains(todo_file, "Task edited", "todos.dat após editar");
    require_binary_contains(todo_file, "Description edited", "todos.dat após editar");
    require_todo_state(todo_file, 1, 4, 0);

    // Buscar, concluir e excluir o item filtrado.
    send_button(display, main_window, 100, 24);
    type_text(display, main_window, "edited");
    send_button(display, main_window, 100, 80);
    send_button(display, main_window, 410, 428);
    sleep_short();
    require_todo_state(todo_file, 1, 4, 1);

    // Bandeja X11 emulada: autorun, esconder, mostrar e sair da primeira
    // execução para validar o carregamento persistente na segunda.
    send_right_click(display, main_window, 700, 200);
    choose_menu(display, 3);
    require_registry_state(work / ".tl_registry_todo", true);
    send_right_click(display, main_window, 700, 200);
    choose_menu(display, 1);
    send_right_click(display, main_window, 700, 200);
    choose_menu(display, 0);
    send_right_click(display, main_window, 700, 200);
    choose_menu(display, 5);

    const int exit_code = wait_guest(child);
    if (exit_code != 0) {
        fail("Simple Todo terminou com código " + std::to_string(exit_code));
    }
    require_run_artifacts(work);

    // Segunda execução: loadTodos precisa restaurar o item concluído; depois
    // o mesmo item é excluído e o autorun é alternado de volta para off.
    const pid_t second_child = start_guest(runtime, executable, display, work);
    const Window second_window = wait_for_window(display, "Todo Application");
    sleep_short();
    send_button(display, second_window, 100, 80);
    send_right_click(display, second_window, 700, 200);
    choose_menu(display, 3);
    require_registry_state(work / ".tl_registry_todo", false);
    send_button(display, second_window, 310, 428);
    sleep_short();
    require_todo_state(todo_file, 0, 0, 0);
    send_right_click(display, second_window, 700, 200);
    choose_menu(display, 5);

    const int second_exit_code = wait_guest(second_child);
    if (second_exit_code != 0) {
        fail("segunda execução do Simple Todo terminou com código " +
             std::to_string(second_exit_code));
    }
    require_run_artifacts(work);
    if (g_xvfb > 0) {
        ::kill(g_xvfb, SIGTERM);
        ::waitpid(g_xvfb, nullptr, 0);
    }
    return 0;
}

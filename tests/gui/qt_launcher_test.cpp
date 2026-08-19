#include "main_window.hpp"

#include "tradutorlinux/catalog/app_catalog.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>

#include <functional>
#include <iostream>
#include <stdexcept>

namespace {

class SmokeFailure final : public std::runtime_error {
public:
    explicit SmokeFailure(const QString& message) : std::runtime_error(message.toStdString()) {}
};

void require(const bool condition, const QString& message) {
    if (!condition) {
        throw SmokeFailure(message);
    }
}

void wait_until(const std::function<bool()>& condition, const int timeout_ms,
                const QString& description) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeout_ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    require(condition(), description);
}

class QtLauncherSmoke final {
public:
    QtLauncherSmoke() {
        require(config_dir_.isValid(), QStringLiteral("diretório temporário inválido"));
        require(QFileInfo::exists(runtime_path()),
                QStringLiteral("o executável tradutorlinux não foi compilado"));
        require(QFileInfo::exists(fixture_path(QStringLiteral("tl_hello.exe"))),
                QStringLiteral("a fixture tl_hello.exe não foi compilada"));
    }

    int run() {
        run_case(QStringLiteral("catálogo, filtro e seleção"),
                 &QtLauncherSmoke::loads_filters_and_selects);
        run_case(QStringLiteral("cadastro e persistência"),
                 &QtLauncherSmoke::registers_and_persists);
        run_case(QStringLiteral("limpeza do formulário"), &QtLauncherSmoke::clears_form);
        run_case(QStringLiteral("relatório assíncrono"), &QtLauncherSmoke::analyzes_report);
        run_case(QStringLiteral("execução e canais de saída"), &QtLauncherSmoke::runs_fixture);
        run_case(QStringLiteral("execução pelo catálogo"), &QtLauncherSmoke::runs_registered_fixture);
        run_case(QStringLiteral("falha ao iniciar runtime"), &QtLauncherSmoke::reports_start_failure);
        return failures_ == 0 ? 0 : 1;
    }

private:
    using Case = void (QtLauncherSmoke::*)();

    void run_case(const QString& name, const Case test_case) {
        try {
            reset_config();
            (this->*test_case)();
            std::cout << "PASS " << name.toStdString() << '\n';
        } catch (const SmokeFailure& failure) {
            ++failures_;
            std::cerr << "FAIL " << name.toStdString() << ": " << failure.what() << '\n';
        }
    }

    void reset_config() {
        QDir config(config_dir_.path());
        require(config.removeRecursively(), QStringLiteral("não foi possível limpar a configuração"));
        require(QDir().mkpath(config_dir_.path()),
                QStringLiteral("não foi possível recriar a configuração"));
        qputenv("XDG_CONFIG_HOME", config_dir_.path().toUtf8());
    }

    [[nodiscard]] QString runtime_path() const {
#ifdef TL_GUI_TEST_RUNTIME_PATH
        return QStringLiteral(TL_GUI_TEST_RUNTIME_PATH);
#else
        return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
            QStringLiteral("../src/tradutorlinux"));
#endif
    }

    [[nodiscard]] QString fixture_path(const QString& name) const {
#ifdef TL_FIXTURE_OUTPUT_DIRECTORY
        return QDir(QStringLiteral(TL_FIXTURE_OUTPUT_DIRECTORY)).filePath(name);
#else
        Q_UNUSED(name);
        return {};
#endif
    }

    void write_catalog(const QList<QPair<QString, QString>>& apps) const {
        tradutorlinux::catalog::AppCatalog catalog;
        for (const auto& [name, executable] : apps) {
            tradutorlinux::catalog::AppEntry entry;
            entry.name = name.toStdString();
            entry.id = tradutorlinux::catalog::AppCatalog::generate_id(entry.name);
            entry.executable_path = executable.toStdString();
            entry.prefix_path = "/tmp/tradutorlinux-test-prefix";
            entry.working_directory = QFileInfo(executable).absolutePath().toStdString();
            require(catalog.add_app(entry), QStringLiteral("falha ao adicionar entrada de teste"));
        }
        require(catalog.save_to_file(), QStringLiteral("falha ao salvar catálogo de teste"));
    }

    [[nodiscard]] QLineEdit* path_input(tradutorlinux::gui::MainWindow& window) const {
        QLineEdit* path = nullptr;
        for (QObject* const child : window.findChildren<QObject*>()) {
            if (child->objectName() == QStringLiteral("path_input")) {
                path = dynamic_cast<QLineEdit*>(child);
                break;
            }
        }
        require(path != nullptr, QStringLiteral("path_input ausente"));
        return path;
    }

    [[nodiscard]] QLineEdit* search_input(tradutorlinux::gui::MainWindow& window) const {
        QLineEdit* search = nullptr;
        for (QObject* const child : window.findChildren<QObject*>()) {
            if (child->objectName() == QStringLiteral("search_input")) {
                search = dynamic_cast<QLineEdit*>(child);
                break;
            }
        }
        require(search != nullptr, QStringLiteral("search_input ausente"));
        return search;
    }

    [[nodiscard]] QListWidget* app_list(tradutorlinux::gui::MainWindow& window) const {
        QListWidget* list = nullptr;
        for (QObject* const child : window.findChildren<QObject*>()) {
            if (child->objectName() == QStringLiteral("app_list")) {
                list = dynamic_cast<QListWidget*>(child);
                break;
            }
        }
        require(list != nullptr, QStringLiteral("app_list ausente"));
        return list;
    }

    [[nodiscard]] QPlainTextEdit* log_output(tradutorlinux::gui::MainWindow& window) const {
        QPlainTextEdit* log = nullptr;
        for (QObject* const child : window.findChildren<QObject*>()) {
            if (child->objectName() == QStringLiteral("log_output")) {
                log = dynamic_cast<QPlainTextEdit*>(child);
                break;
            }
        }
        require(log != nullptr, QStringLiteral("log_output ausente"));
        return log;
    }

    [[nodiscard]] QLabel* status_label(tradutorlinux::gui::MainWindow& window) const {
        QLabel* status = nullptr;
        for (QObject* const child : window.findChildren<QObject*>()) {
            if (child->objectName() == QStringLiteral("status_label")) {
                status = dynamic_cast<QLabel*>(child);
                break;
            }
        }
        require(status != nullptr, QStringLiteral("status_label ausente"));
        return status;
    }

    [[nodiscard]] QPushButton* button(tradutorlinux::gui::MainWindow& window,
                                      const char* const object_name) const {
        QPushButton* result = nullptr;
        for (QObject* const child : window.findChildren<QObject*>()) {
            if (child->objectName() == QString::fromLatin1(object_name)) {
                result = dynamic_cast<QPushButton*>(child);
                break;
            }
        }
        require(result != nullptr, QStringLiteral("botão ausente: ") + QString::fromLatin1(object_name));
        return result;
    }

    void loads_filters_and_selects() {
        write_catalog({{QStringLiteral("Alpha Tool"), QStringLiteral("/tmp/alpha.exe")},
                       {QStringLiteral("Beta Tool"), QStringLiteral("/tmp/beta.exe")}});
        tradutorlinux::gui::MainWindow window(runtime_path());
        window.show();
        QApplication::processEvents();

        QListWidget* const list = app_list(window);
        QLineEdit* const search = search_input(window);
        QLineEdit* const path = path_input(window);
        require(list->count() == 2, QStringLiteral("catálogo não carregou duas entradas"));
        search->setText(QStringLiteral("Beta"));
        QApplication::processEvents();
        require(list->item(0)->isHidden(), QStringLiteral("filtro não ocultou Alpha"));
        require(!list->item(1)->isHidden(), QStringLiteral("filtro ocultou Beta"));
        list->setCurrentRow(1);
        require(path->text() == QStringLiteral("/tmp/beta.exe"),
                QStringLiteral("seleção não preencheu o caminho"));
    }

    void registers_and_persists() {
        tradutorlinux::gui::MainWindow window(runtime_path());
        window.show();
        path_input(window)->setText(QStringLiteral("/tmp/My Tool.exe"));
        button(window, "register_button")->click();
        QApplication::processEvents();

        tradutorlinux::catalog::AppCatalog catalog;
        require(catalog.load_from_file(), QStringLiteral("catálogo não foi salvo"));
        const auto app = catalog.find_app("my_tool");
        require(app.has_value(), QStringLiteral("aplicativo cadastrado não foi encontrado"));
        require(app->executable_path == "/tmp/My Tool.exe",
                QStringLiteral("caminho cadastrado incorreto"));
        require(!app->prefix_path.empty(), QStringLiteral("prefixo não foi preservado"));

        QListWidget* const list = app_list(window);
        list->setCurrentRow(0);
        path_input(window)->setText(QStringLiteral("/tmp/Updated Tool.exe"));
        button(window, "register_button")->click();
        QApplication::processEvents();

        tradutorlinux::catalog::AppCatalog updated_catalog;
        require(updated_catalog.load_from_file(), QStringLiteral("catálogo atualizado não foi salvo"));
        const auto updated = updated_catalog.find_app("my_tool");
        require(updated.has_value(), QStringLiteral("ID do aplicativo não foi preservado"));
        require(updated->executable_path == "/tmp/Updated Tool.exe",
                QStringLiteral("atualização não alterou o caminho"));
        require(updated->prefix_path == app->prefix_path,
                QStringLiteral("atualização perdeu o prefixo"));
        require(updated->working_directory == app->working_directory,
                QStringLiteral("atualização perdeu o diretório de trabalho"));
    }

    void clears_form() {
        write_catalog({{QStringLiteral("Saved Tool"), QStringLiteral("/tmp/saved.exe")}});
        tradutorlinux::gui::MainWindow window(runtime_path());
        window.show();
        search_input(window)->setText(QStringLiteral("Saved"));
        path_input(window)->setText(QStringLiteral("/tmp/temporary.exe"));
        button(window, "clear_button")->click();
        require(search_input(window)->text().isEmpty(), QStringLiteral("busca não foi limpa"));
        require(path_input(window)->text().isEmpty(), QStringLiteral("caminho não foi limpo"));
        require(app_list(window)->count() == 1, QStringLiteral("limpeza perdeu a biblioteca"));
        require(log_output(window)->toPlainText().contains(QStringLiteral("Selecione um executável")),
                QStringLiteral("mensagem inicial não foi restaurada"));
    }

    void analyzes_report() {
        tradutorlinux::gui::MainWindow window(runtime_path());
        window.show();
        path_input(window)->setText(fixture_path(QStringLiteral("tl_hello.exe")));
        button(window, "analyze_button")->click();
        require(!button(window, "run_button")->isEnabled(),
                QStringLiteral("ações conflitantes não foram desabilitadas"));
        wait_until([this, &window] {
            return status_label(window)->text() == QStringLiteral("Operação concluída");
        }, 10000, QStringLiteral("relatório não terminou com sucesso"));
        const QString output = log_output(window)->toPlainText();
        require(output.contains(QStringLiteral("TradutorLinux compatibility report")),
                QStringLiteral("relatório não apareceu no console"));
        require(output.contains(QStringLiteral("execution-result: not-attempted")),
                QStringLiteral("resultado do relatório não apareceu"));
        require(output.contains(QStringLiteral("[stderr]")),
                QStringLiteral("stderr não foi capturado"));
    }

    void runs_fixture() {
        tradutorlinux::gui::MainWindow window(runtime_path());
        window.show();
        path_input(window)->setText(fixture_path(QStringLiteral("tl_hello.exe")));
        button(window, "run_button")->click();
        wait_until([this, &window] {
            return status_label(window)->text() == QStringLiteral("Operação concluída");
        }, 10000, QStringLiteral("fixture não terminou com sucesso"));
        const QString output = log_output(window)->toPlainText();
        require(output.contains(QStringLiteral("[stdout] Ola do Windows no Linux!")),
                QStringLiteral("stdout do convidado não foi capturado"));
        require(output.contains(QStringLiteral("[stderr]")),
                QStringLiteral("stderr do runtime não foi capturado"));
        require(output.contains(QStringLiteral("[launcher] código de saída: 0")),
                QStringLiteral("código de saída não apareceu"));
    }

    void runs_registered_fixture() {
        const QString fixture = fixture_path(QStringLiteral("tl_hello.exe"));
        write_catalog({{QStringLiteral("Registered Hello"), fixture}});
        tradutorlinux::gui::MainWindow window(runtime_path());
        window.show();
        app_list(window)->setCurrentRow(0);
        button(window, "run_button")->click();
        wait_until([this, &window] {
            return status_label(window)->text() == QStringLiteral("Operação concluída");
        }, 10000, QStringLiteral("fixture cadastrada não terminou com sucesso"));
        const QString output = log_output(window)->toPlainText();
        require(output.contains(QStringLiteral("app run registered_hello --trace")),
                QStringLiteral("execução não usou o ID do catálogo"));
        require(output.contains(QStringLiteral("[stdout] Ola do Windows no Linux!")),
                QStringLiteral("stdout da fixture cadastrada não foi capturado"));
    }

    void reports_start_failure() {
        tradutorlinux::gui::MainWindow window(
            QStringLiteral("/definitely/missing/tradutorlinux"));
        window.show();
        path_input(window)->setText(fixture_path(QStringLiteral("tl_hello.exe")));
        button(window, "analyze_button")->click();
        wait_until([this, &window] {
            return status_label(window)->text() == QStringLiteral("Falha ao iniciar o runtime");
        }, 5000, QStringLiteral("falha de inicialização não foi reportada"));
        require(log_output(window)->toPlainText().contains(QStringLiteral("Erro ao iniciar o runtime")),
                QStringLiteral("detalhe da falha não apareceu"));
    }

    QTemporaryDir config_dir_;
    int failures_{0};
};

}  // namespace

int main(int argc, char* argv[]) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication application(argc, argv);
    QtLauncherSmoke smoke;
    return smoke.run();
}

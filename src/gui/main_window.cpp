#include "main_window.hpp"

#include "tradutorlinux/cli.hpp"
#include "tradutorlinux/prefix/prefix.hpp"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QAbstractItemView>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include <filesystem>
#include <string>
#include <utility>

namespace tradutorlinux::gui {
namespace {

constexpr int kDefaultWidth = 1080;
constexpr int kDefaultHeight = 760;
constexpr int kMaximumLogBlocks = 10000;

constexpr int kAppPathRole = Qt::UserRole + 1;

[[nodiscard]] QString qstring_from_path(const std::filesystem::path& path) {
    return QString::fromStdString(path.string());
}

[[nodiscard]] QString qstring_from_std_string(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] std::filesystem::path path_from_qstring(const QString& value) {
    return std::filesystem::path(value.toStdString());
}

}  // namespace

MainWindow::MainWindow(QWidget* const parent) : MainWindow(QString{}, parent) {}

MainWindow::MainWindow(const QString& runtime_path, QWidget* const parent)
    : QMainWindow(parent), process_(this), runtime_path_(runtime_path) {
    setup_ui();
    refresh_app_list();
    set_status(QStringLiteral("Pronto para analisar ou executar"));
    append_message(QStringLiteral(
        "Selecione um executável Windows e escolha Analisar, Executar ou Cadastrar na biblioteca."));
    update_action_state();
}

void MainWindow::setup_ui() {
    setWindowTitle(QStringLiteral("TradutorLinux — Launcher"));
    resize(kDefaultWidth, kDefaultHeight);
    setMinimumSize(820, 560);

    auto* const central = new QWidget(this);
    auto* const root_layout = new QVBoxLayout(central);
    root_layout->setContentsMargins(24, 20, 24, 20);
    root_layout->setSpacing(12);

    auto* const title = new QLabel(QStringLiteral("TradutorLinux"), central);
    title->setObjectName(QStringLiteral("title_label"));
    title->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 700;"));
    root_layout->addWidget(title);

    auto* const subtitle = new QLabel(
        QStringLiteral("Runtime Win32 e biblioteca de aplicativos"), central);
    subtitle->setObjectName(QStringLiteral("subtitle_label"));
    subtitle->setStyleSheet(QStringLiteral("color: #64748b;"));
    root_layout->addWidget(subtitle);

    auto* const splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setObjectName(QStringLiteral("content_splitter"));

    auto* const library_box = new QGroupBox(QStringLiteral("Biblioteca"), splitter);
    auto* const library_layout = new QVBoxLayout(library_box);
    library_layout->setContentsMargins(12, 16, 12, 12);

    search_input_ = new QLineEdit(library_box);
    search_input_->setObjectName(QStringLiteral("search_input"));
    search_input_->setPlaceholderText(QStringLiteral("Buscar aplicativo..."));
    library_layout->addWidget(search_input_);

    app_list_ = new QListWidget(library_box);
    app_list_->setObjectName(QStringLiteral("app_list"));
    app_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    app_list_->setAlternatingRowColors(true);
    library_layout->addWidget(app_list_, 1);

    app_count_label_ = new QLabel(library_box);
    app_count_label_->setObjectName(QStringLiteral("app_count_label"));
    app_count_label_->setStyleSheet(QStringLiteral("color: #64748b;"));
    library_layout->addWidget(app_count_label_);

    auto* const action_box = new QGroupBox(QStringLiteral("Executável"), splitter);
    auto* const action_layout = new QVBoxLayout(action_box);
    action_layout->setContentsMargins(12, 16, 12, 12);

    auto* const path_label = new QLabel(QStringLiteral("Arquivo PE32+ x86-64"), action_box);
    action_layout->addWidget(path_label);

    auto* const path_row = new QHBoxLayout();
    path_input_ = new QLineEdit(action_box);
    path_input_->setObjectName(QStringLiteral("path_input"));
    path_input_->setPlaceholderText(QStringLiteral("Informe o caminho do arquivo .exe"));
    path_row->addWidget(path_input_, 1);

    auto* const choose_button = new QPushButton(QStringLiteral("Escolher..."), action_box);
    choose_button->setObjectName(QStringLiteral("choose_button"));
    path_row->addWidget(choose_button);
    action_layout->addLayout(path_row);

    install_name_input_ = new QLineEdit(action_box);
    install_name_input_->setObjectName(QStringLiteral("install_name_input"));
    install_name_input_->setPlaceholderText(
        QStringLiteral("Nome na biblioteca (opcional para instalar)"));
    action_layout->addWidget(install_name_input_);

    auto* const hint = new QLabel(
        QStringLiteral("Aplicativos cadastrados usam o prefixo e o diretório do catálogo."),
        action_box);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #64748b;"));
    action_layout->addWidget(hint);

    auto* const buttons_layout = new QGridLayout();
    analyze_button_ = new QPushButton(QStringLiteral("Analisar"), action_box);
    analyze_button_->setObjectName(QStringLiteral("analyze_button"));
    run_button_ = new QPushButton(QStringLiteral("Executar"), action_box);
    run_button_->setObjectName(QStringLiteral("run_button"));
    install_button_ = new QPushButton(QStringLiteral("Instalar"), action_box);
    install_button_->setObjectName(QStringLiteral("install_button"));
    register_button_ = new QPushButton(QStringLiteral("Cadastrar"), action_box);
    register_button_->setObjectName(QStringLiteral("register_button"));
    clear_button_ = new QPushButton(QStringLiteral("Limpar"), action_box);
    clear_button_->setObjectName(QStringLiteral("clear_button"));
    exit_button_ = new QPushButton(QStringLiteral("Sair"), action_box);
    exit_button_->setObjectName(QStringLiteral("exit_button"));
    buttons_layout->addWidget(analyze_button_, 0, 0);
    buttons_layout->addWidget(run_button_, 0, 1);
    buttons_layout->addWidget(install_button_, 1, 0);
    buttons_layout->addWidget(register_button_, 1, 1);
    buttons_layout->addWidget(clear_button_, 2, 0);
    buttons_layout->addWidget(exit_button_, 2, 1);
    action_layout->addLayout(buttons_layout);
    action_layout->addStretch(1);

    splitter->addWidget(library_box);
    splitter->addWidget(action_box);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    root_layout->addWidget(splitter, 1);

    auto* const status_row = new QHBoxLayout();
    auto* const status_caption = new QLabel(QStringLiteral("Status:"), central);
    status_caption->setStyleSheet(QStringLiteral("font-weight: 600;"));
    status_row->addWidget(status_caption);
    status_label_ = new QLabel(central);
    status_label_->setObjectName(QStringLiteral("status_label"));
    status_row->addWidget(status_label_, 1);
    root_layout->addLayout(status_row);

    auto* const log_box = new QGroupBox(QStringLiteral("Diagnóstico e execução"), central);
    auto* const log_layout = new QVBoxLayout(log_box);
    log_layout->setContentsMargins(12, 16, 12, 12);
    log_output_ = new QPlainTextEdit(log_box);
    log_output_->setObjectName(QStringLiteral("log_output"));
    log_output_->setReadOnly(true);
    log_output_->setLineWrapMode(QPlainTextEdit::NoWrap);
    log_output_->setMaximumBlockCount(kMaximumLogBlocks);
    log_output_->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: #111827; color: #e2e8f0; "
        "font-family: monospace; }"));
    log_layout->addWidget(log_output_);
    root_layout->addWidget(log_box, 1);

    setCentralWidget(central);

    connect(search_input_, &QLineEdit::textChanged, this,
            &MainWindow::on_search_text_changed);
    connect(app_list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* const current, QListWidgetItem* const) {
                on_app_selected(current);
            });
    connect(path_input_, &QLineEdit::textChanged, this, &MainWindow::on_path_text_changed);
    connect(choose_button, &QPushButton::clicked, this, &MainWindow::on_choose_clicked);
    connect(analyze_button_, &QPushButton::clicked, this, &MainWindow::on_analyze_clicked);
    connect(run_button_, &QPushButton::clicked, this, &MainWindow::on_run_clicked);
    connect(install_button_, &QPushButton::clicked, this, &MainWindow::on_install_clicked);
    connect(register_button_, &QPushButton::clicked, this,
            &MainWindow::on_register_clicked);
    connect(clear_button_, &QPushButton::clicked, this, &MainWindow::on_clear_clicked);
    connect(exit_button_, &QPushButton::clicked, this, &QWidget::close);
    connect(&process_, &QProcess::readyReadStandardOutput, this,
            &MainWindow::on_process_stdout_ready);
    connect(&process_, &QProcess::readyReadStandardError, this,
            &MainWindow::on_process_stderr_ready);
    connect(&process_, &QProcess::errorOccurred, this, &MainWindow::on_process_error);
    connect(&process_, &QProcess::stateChanged, this,
            [this](QProcess::ProcessState) { update_action_state(); });
    connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &MainWindow::on_process_finished);
}

void MainWindow::refresh_app_list() {
    const QString previous_selection = selected_app_id_;
    selected_app_id_.clear();
    app_list_->clear();
    const bool loaded = catalog_.load_from_file();
    const auto& apps = catalog_.list_apps();

    for (const catalog::AppEntry& app : apps) {
        auto* const item = new QListWidgetItem(qstring_from_std_string(app.name), app_list_);
        item->setData(Qt::UserRole, qstring_from_std_string(app.id));
        item->setData(kAppPathRole, qstring_from_std_string(app.executable_path));
        item->setToolTip(qstring_from_std_string(app.executable_path));
    }

    if (!previous_selection.isEmpty()) {
        for (int index = 0; index < app_list_->count(); ++index) {
            QListWidgetItem* const item = app_list_->item(index);
            if (item->data(Qt::UserRole).toString() == previous_selection) {
                app_list_->setCurrentItem(item);
                break;
            }
        }
    }

    app_count_label_->setText(
        QStringLiteral("%1 aplicativo(s) cadastrado(s)").arg(apps.size()));
    if (!loaded && QFileInfo::exists(qstring_from_path(catalog::AppCatalog::default_catalog_path()))) {
        append_message(QStringLiteral("Aviso: não foi possível carregar library.json."));
    }
}

void MainWindow::on_search_text_changed(const QString& query) {
    for (int index = 0; index < app_list_->count(); ++index) {
        QListWidgetItem* const item = app_list_->item(index);
        const bool matches = item->text().contains(query, Qt::CaseInsensitive) ||
                             item->toolTip().contains(query, Qt::CaseInsensitive);
        item->setHidden(!matches);
    }
}

void MainWindow::on_app_selected(QListWidgetItem* const item) {
    if (item == nullptr) {
        return;
    }
    selected_app_id_ = item->data(Qt::UserRole).toString();
    const QSignalBlocker blocker(path_input_);
    path_input_->setText(item->data(kAppPathRole).toString());
    set_status(QStringLiteral("Aplicativo selecionado"));
    update_action_state();
}

void MainWindow::on_path_text_changed(const QString& path) {
    Q_UNUSED(path);
    update_action_state();
}

void MainWindow::on_choose_clicked() {
    const QString selected = QFileDialog::getOpenFileName(
        this, QStringLiteral("Selecionar executável Windows"),
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
        QStringLiteral("Executáveis Windows (*.exe *.EXE);;Todos os arquivos (*)"));
    if (selected.isEmpty()) {
        return;
    }

    selected_app_id_.clear();
    path_input_->setText(selected);
    set_status(QStringLiteral("Arquivo selecionado"));
    append_message(QStringLiteral("Arquivo selecionado: %1").arg(selected));
}

void MainWindow::on_analyze_clicked() {
    start_runtime(true);
}

void MainWindow::on_run_clicked() {
    start_runtime(false);
}

void MainWindow::on_install_clicked() {
    start_install();
}

void MainWindow::on_register_clicked() {
    const QString path_text = path_input_->text().trimmed();
    if (path_text.isEmpty()) {
        set_status(QStringLiteral("Informe um executável antes de cadastrar."));
        append_message(QStringLiteral("Erro: nenhum executável foi informado."));
        return;
    }

    catalog::AppEntry entry;
    const auto selected = catalog_.find_app(selected_app_id_.toStdString());
    if (!selected_app_id_.isEmpty() && selected.has_value()) {
        entry = *selected;
        entry.executable_path = path_text.toStdString();
    } else {
        const std::filesystem::path executable = path_from_qstring(path_text);
        entry.name = executable.filename().string();
        entry.id = catalog::AppCatalog::generate_id(entry.name);
        for (unsigned int suffix = 2U; catalog_.find_app(entry.id).has_value(); ++suffix) {
            entry.id = catalog::AppCatalog::generate_id(entry.name) + "-" +
                       std::to_string(suffix);
        }
        entry.executable_path = executable.string();
        entry.prefix_path = prefix::default_app_prefix(entry.id).string();
        if (!prefix::initialize_prefix(entry.prefix_path)) {
            set_status(QStringLiteral("Erro ao preparar prefixo"));
            append_message(QStringLiteral("Não foi possível criar o prefixo do aplicativo."));
            return;
        }
        const std::filesystem::path executable_parent = executable.parent_path();
        entry.working_directory =
            !executable_parent.empty() && std::filesystem::is_directory(executable_parent)
                ? executable_parent.string()
                : prefix::get_environment_paths(entry.prefix_path).drive_c.string();
    }

    if (!catalog_.add_app(entry) || !catalog_.save_to_file()) {
        set_status(QStringLiteral("Erro ao cadastrar aplicativo"));
        append_message(QStringLiteral("Não foi possível salvar o aplicativo na biblioteca."));
        return;
    }

    if (!catalog::AppCatalog::create_desktop_entry(entry)) {
        append_message(QStringLiteral("Aviso: não foi possível criar o atalho do aplicativo."));
    }

    selected_app_id_ = qstring_from_std_string(entry.id);
    refresh_app_list();
    set_status(QStringLiteral("Cadastrado na biblioteca"));
    append_message(QStringLiteral("Aplicativo '%1' adicionado com sucesso [id: %2].")
                       .arg(qstring_from_std_string(entry.name),
                            qstring_from_std_string(entry.id)));
    append_message(QStringLiteral("Arquivo salvo em: %1")
                       .arg(qstring_from_path(catalog::AppCatalog::default_catalog_path())));
    update_action_state();
}

void MainWindow::on_clear_clicked() {
    selected_app_id_.clear();
    path_input_->clear();
    install_name_input_->clear();
    search_input_->clear();
    app_list_->clearSelection();
    log_output_->clear();
    set_status(QStringLiteral("Pronto para analisar ou executar"));
    append_message(QStringLiteral(
        "Selecione um executável Windows e escolha Analisar, Executar ou Cadastrar na biblioteca."));
    refresh_app_list();
    update_action_state();
}

void MainWindow::on_process_stdout_ready() {
    append_log(QStringLiteral("stdout"), process_.readAllStandardOutput());
}

void MainWindow::on_process_stderr_ready() {
    const QByteArray bytes = process_.readAllStandardError();
    if (install_in_progress_) {
        install_stderr_.append(QString::fromUtf8(bytes));
    }
    append_log(QStringLiteral("stderr"), bytes);
}

void MainWindow::on_process_error(const QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart) {
        return;
    }
    process_start_failed_ = true;
    set_status(QStringLiteral("Falha ao iniciar o runtime"));
    append_message(QStringLiteral("Erro ao iniciar o runtime: %1").arg(process_.errorString()));
    update_action_state();
}

void MainWindow::on_process_finished(const int exit_code,
                                     const QProcess::ExitStatus status) {
    consume_process_output();
    if (process_start_failed_) {
        process_start_failed_ = false;
        install_in_progress_ = false;
        catalog_registration_in_progress_ = false;
        update_action_state();
        return;
    }

    if (status == QProcess::CrashExit) {
        set_status(QStringLiteral("Runtime terminou por sinal"));
        append_message(QStringLiteral("[launcher] processo terminou por sinal ou crash."));
    } else {
        if (install_in_progress_) {
            finish_install(exit_code);
        } else if (catalog_registration_in_progress_) {
            catalog_registration_in_progress_ = false;
            if (exit_code == 0) {
                refresh_app_list();
                set_status(QStringLiteral("Aplicativo cadastrado após a instalação"));
            } else {
                set_status(QStringLiteral("Cadastro após instalação falhou"));
            }
        } else {
            set_status(exit_code == 0 ? QStringLiteral("Operação concluída")
                                      : QStringLiteral("Operação terminou com erro"));
        }
        append_message(QStringLiteral("[launcher] código de saída: %1").arg(exit_code));
    }
    update_action_state();
}

void MainWindow::closeEvent(QCloseEvent* const event) {
    if (process_is_running()) {
        process_.terminate();
        if (!process_.waitForFinished(1000)) {
            process_.kill();
            (void)process_.waitForFinished(1000);
        }
    }
    event->accept();
}

void MainWindow::update_action_state() {
    const bool has_path = !path_input_->text().trimmed().isEmpty();
    const bool running = process_is_running();
    analyze_button_->setEnabled(has_path && !running);
    run_button_->setEnabled(has_path && !running);
    install_button_->setEnabled(has_path && !running);
    register_button_->setEnabled(has_path && !running);
    clear_button_->setEnabled(!running);
    exit_button_->setEnabled(true);
    search_input_->setEnabled(!running);
    app_list_->setEnabled(!running);
    install_name_input_->setEnabled(!running);
}

void MainWindow::append_log(const QString& channel, const QByteArray& bytes) {
    if (bytes.isEmpty()) {
        return;
    }
    QString text = QString::fromUtf8(bytes);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QChar('\r'), QChar('\n'));
    const QStringList lines = text.split(QChar('\n'));
    for (const QString& line : lines) {
        if (!line.isEmpty()) {
            log_output_->appendPlainText(QStringLiteral("[%1] %2").arg(channel, line));
        }
    }
}

void MainWindow::append_message(const QString& message) {
    log_output_->appendPlainText(QStringLiteral("[launcher] %1").arg(message));
}

void MainWindow::set_status(const QString& status) {
    status_label_->setText(status);
}

void MainWindow::consume_process_output() {
    on_process_stdout_ready();
    on_process_stderr_ready();
}

void MainWindow::start_runtime(const bool report_only) {
    const QString path = selected_or_direct_path();
    if (path.isEmpty()) {
        set_status(QStringLiteral("Informe um executável antes de continuar."));
        append_message(QStringLiteral("Erro: nenhum executável foi informado."));
        return;
    }
    if (process_is_running()) {
        append_message(QStringLiteral("Aviso: um programa já está em execução."));
        return;
    }

    const QString program = runtime_executable();
    install_in_progress_ = false;
    catalog_registration_in_progress_ = false;
    const auto selected = catalog_.find_app(selected_app_id_.toStdString());
    const bool use_catalog_entry = !selected_app_id_.isEmpty() && selected.has_value() &&
                                   qstring_from_std_string(selected->executable_path) == path;
    QStringList arguments;
    QString working_directory;
    if (use_catalog_entry) {
        arguments << QStringLiteral("app") << QStringLiteral("run") << selected_app_id_
                  << QStringLiteral("--trace");
        if (report_only) {
            arguments << QStringLiteral("--report");
        }
        working_directory = qstring_from_std_string(selected->working_directory);
    } else {
        arguments << QStringLiteral("--trace");
        if (report_only) {
            arguments << QStringLiteral("--report");
        }
        arguments << path;
        working_directory = QFileInfo(path).absolutePath();
    }

    if (!working_directory.isEmpty() && QDir(working_directory).exists()) {
        process_.setWorkingDirectory(working_directory);
    } else {
        process_.setWorkingDirectory(QDir::currentPath());
    }

    process_start_failed_ = false;
    set_status(report_only ? QStringLiteral("Analisando...") : QStringLiteral("Executando..."));
    append_message(QStringLiteral("Iniciando: %1 %2").arg(program, arguments.join(QChar(' '))));
    process_.start(program, arguments);
    update_action_state();
}

void MainWindow::start_install() {
    const QString path = selected_or_direct_path();
    if (path.isEmpty()) {
        set_status(QStringLiteral("Informe um instalador antes de continuar."));
        append_message(QStringLiteral("Erro: nenhum instalador foi informado."));
        return;
    }
    if (process_is_running()) {
        append_message(QStringLiteral("Aviso: um programa já está em execução."));
        return;
    }

    install_stderr_.clear();
    install_in_progress_ = true;
    pending_install_name_ = install_name_input_->text().trimmed();
    if (pending_install_name_.isEmpty()) {
        pending_install_name_ = QFileInfo(path).completeBaseName();
    }

    QStringList arguments;
    arguments << QStringLiteral("install") << path << QStringLiteral("--trace");
    if (!install_name_input_->text().trimmed().isEmpty()) {
        arguments << QStringLiteral("--name") << install_name_input_->text().trimmed();
    }
    process_.setWorkingDirectory(QFileInfo(path).absolutePath());
    process_start_failed_ = false;
    set_status(QStringLiteral("Instalando..."));
    append_message(QStringLiteral("Iniciando instalação: %1 %2")
                       .arg(runtime_executable(), arguments.join(QChar(' '))));
    process_.start(runtime_executable(), arguments);
    update_action_state();
}

void MainWindow::start_catalog_registration(const QString& executable, const QString& prefix,
                                            const QString& app_id, const QString& app_name) {
    if (process_is_running()) {
        return;
    }
    QStringList arguments;
    arguments << QStringLiteral("app") << QStringLiteral("add") << executable
              << QStringLiteral("--name") << app_name
              << QStringLiteral("--prefix") << prefix
              << QStringLiteral("--id") << app_id;
    catalog_registration_in_progress_ = true;
    process_start_failed_ = false;
    set_status(QStringLiteral("Cadastrando executável escolhido..."));
    append_message(QStringLiteral("Cadastrando executável escolhido: %1").arg(executable));
    process_.start(runtime_executable(), arguments);
    update_action_state();
}

void MainWindow::finish_install(const int exit_code) {
    install_in_progress_ = false;
    if (exit_code == 0) {
        const QRegularExpression id_expression(QStringLiteral("app-id=\\\"([^\\\"]+)\\\""));
        const QRegularExpressionMatch id_match = id_expression.match(install_stderr_);
        if (id_match.hasMatch()) {
            selected_app_id_ = id_match.captured(1);
        }
        refresh_app_list();
        set_status(QStringLiteral("Instalação concluída e aplicativo cadastrado"));
        return;
    }

    if (exit_code != static_cast<int>(ExitCode::InstallPending)) {
        set_status(QStringLiteral("Instalação terminou com erro"));
        return;
    }

    const QRegularExpression prefix_expression(QStringLiteral("prefix=\\\"([^\\\"]+)\\\""));
    const QRegularExpression id_expression(QStringLiteral("app-id=\\\"([^\\\"]+)\\\""));
    const QRegularExpression candidate_expression(QStringLiteral("candidate path=\\\"([^\\\"]+)\\\""));
    const QRegularExpressionMatch prefix_match = prefix_expression.match(install_stderr_);
    const QRegularExpressionMatch id_match = id_expression.match(install_stderr_);
    QRegularExpressionMatchIterator candidate_matches = candidate_expression.globalMatch(install_stderr_);
    QStringList candidates;
    while (candidate_matches.hasNext()) {
        candidates << candidate_matches.next().captured(1);
    }

    if (!prefix_match.hasMatch() || !id_match.hasMatch() || candidates.isEmpty()) {
        set_status(QStringLiteral("Instalação concluída; cadastro pendente"));
        append_message(QStringLiteral("Nenhum executável final foi identificado automaticamente."));
        return;
    }

    bool accepted = false;
    const QString selected = QInputDialog::getItem(
        this, QStringLiteral("Escolher executável instalado"),
        QStringLiteral("Mais de um executável PE32+ x64 foi instalado:"), candidates, 0, false,
        &accepted);
    if (!accepted || selected.isEmpty()) {
        set_status(QStringLiteral("Instalação concluída; cadastro pendente"));
        append_message(QStringLiteral("Nenhum executável foi escolhido para cadastro."));
        return;
    }
    start_catalog_registration(selected, prefix_match.captured(1), id_match.captured(1),
                               pending_install_name_);
}

QString MainWindow::runtime_executable() const {
    if (!runtime_path_.isEmpty()) {
        return runtime_path_;
    }
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tradutorlinux"));
}

QString MainWindow::selected_or_direct_path() const {
    return path_input_->text().trimmed();
}

bool MainWindow::process_is_running() const noexcept {
    return process_.state() != QProcess::NotRunning;
}

}  // namespace tradutorlinux::gui

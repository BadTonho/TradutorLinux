#pragma once

#include "tradutorlinux/catalog/app_catalog.hpp"

#include <QMainWindow>
#include <QProcess>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QCloseEvent;

namespace tradutorlinux::gui {

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    explicit MainWindow(const QString& runtime_path, QWidget* parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void on_search_text_changed(const QString& query);
    void on_app_selected(QListWidgetItem* item);
    void on_path_text_changed(const QString& path);
    void on_choose_clicked();
    void on_analyze_clicked();
    void on_run_clicked();
    void on_install_clicked();
    void on_register_clicked();
    void on_doctor_clicked();
    void on_clear_clicked();
    void on_process_stdout_ready();
    void on_process_stderr_ready();
    void on_process_error(QProcess::ProcessError error);
    void on_process_finished(int exit_code, QProcess::ExitStatus status);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setup_ui();
    void refresh_app_list();
    void update_action_state();
    void append_log(const QString& channel, const QByteArray& bytes);
    void append_message(const QString& message);
    void set_status(const QString& status);
    void consume_process_output();
    void start_runtime(bool report_only);
    void start_install();
    void start_catalog_registration(const QString& executable, const QString& prefix,
                                    const QString& app_id, const QString& app_name);
    void finish_install(int exit_code);
    [[nodiscard]] QString runtime_executable() const;
    [[nodiscard]] QString selected_or_direct_path() const;
    [[nodiscard]] bool process_is_running() const noexcept;

    QLineEdit* search_input_{nullptr};
    QListWidget* app_list_{nullptr};
    QLabel* app_count_label_{nullptr};
    QLineEdit* path_input_{nullptr};
    QLineEdit* install_name_input_{nullptr};
    QLabel* status_label_{nullptr};
    QPlainTextEdit* log_output_{nullptr};
    QPushButton* analyze_button_{nullptr};
    QPushButton* run_button_{nullptr};
    QPushButton* install_button_{nullptr};
    QPushButton* register_button_{nullptr};
    QPushButton* doctor_button_{nullptr};
    QPushButton* clear_button_{nullptr};
    QPushButton* exit_button_{nullptr};

    catalog::AppCatalog catalog_;
    QProcess process_;
    QString runtime_path_;
    QString selected_app_id_;
    QString install_stderr_;
    QString pending_install_name_;
    bool process_start_failed_{false};
    bool install_in_progress_{false};
    bool catalog_registration_in_progress_{false};
    bool doctor_in_progress_{false};
};

}  // namespace tradutorlinux::gui

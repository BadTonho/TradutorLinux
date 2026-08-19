# Guia de Implementação da Interface Desktop com Qt6

Este documento estabelece o guia prático e a arquitetura para substituir a interface experimental em Xlib puro ([src/gui/launcher.cpp](file:///c:/Users/Admin/Desktop/ProjetosCode/Linux/TradutorLinux/src/gui/launcher.cpp)) por uma aplicação desktop moderna, robusta e modular utilizando **Qt6 (C++)**.

---

## 1. Por Que Migrar do Xlib para o Qt6?

| Desafio no Xlib Puro | Solução com Qt6 |
|---|---|
| Cálculo manual de pixels (`kInputY = 190`, `kButtonY = 246`) | Layouts dinâmicos e responsivos (`QVBoxLayout`, `QHBoxLayout`) |
| Detecção manual de cliques em coordenadas | Sistema moderno de Sinais e Slots (`connect(button, &QPushButton::clicked, ...)`) |
| Dificuldade para criar listas com barra de rolagem | `QListWidget` / `QTableView` com scroll e seleção prontos |
| Janela trava durante a execução (`waitpid` síncrono) | Execução assíncrona em segundo plano com `QProcess` |
| Dependência de chamar `zenity` via `fork()` para escolher arquivos | Diálogo de arquivos nativo do sistema (`QFileDialog`) |
| Sem suporte a atalhos (Ctrl+C, Ctrl+V, seleção de texto) | Suporte completo e nativo a teclado, mouse e clipboard |

---

## 2. Arquitetura Desacoplada (Frontend Qt6 + Backend Core)

A interface gráfica passa a ser uma camada de apresentação pura, consumindo os módulos já implementados no Core:

```text
┌─────────────────────────────────────────────────────────────┐
│                 Interface Desktop (Qt6 GUI)                 │
│  - MainWindow: Barra de busca, Grade de apps, Botões        │
│  - LogConsole: Terminal com saída colorida e status         │
│  - InstallWizard: Assistente de instalação de executáveis   │
└──────────────────────────────┬──────────────────────────────┘
                               │ (Chama APIs C++)
                               ▼
┌─────────────────────────────────────────────────────────────┐
│             Serviços e Core do TradutorLinux                │
│  ├─ AppCatalog: Leitura/Escrita de library.json             │
│  ├─ Prefix: Gerenciamento do drive virtual C:\              │
│  ├─ PE Parser & Loader: Inspeção de imagens e imports       │
│  └─ Process Isolate: Execução do binário convidado          │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. Pacotes Necessários no Linux

Para compilar a interface Qt6 no Linux, os seguintes pacotes de desenvolvimento são necessários:

### Ubuntu / Debian / Pop!_OS
```bash
sudo apt update
sudo apt install qt6-base-dev qt6-tools-dev libgl1-mesa-dev
```

### Fedora / RHEL
```bash
sudo dnf install qt6-qtbase-devel
```

### Arch Linux / Manjaro
```bash
sudo pacman -S qt6-base
```

---

## 4. Configuração no `CMakeLists.txt`

Ajuste no arquivo [src/CMakeLists.txt](file:///c:/Users/Admin/Desktop/ProjetosCode/Linux/TradutorLinux/src/CMakeLists.txt):

```cmake
# Habilitar automação do Qt (MOC)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Encontrar componentes do Qt6
find_package(Qt6 COMPONENTS Widgets REQUIRED)

# Executável do Launcher Desktop
add_executable(tradutorlinux_gui
    gui/qt_main.cpp
    gui/main_window.cpp
    gui/main_window.hpp
)

target_link_libraries(tradutorlinux_gui PRIVATE
    tradutorlinux_core
    Qt6::Widgets
)

tl_enable_warnings(tradutorlinux_gui)
tl_enable_sanitizers(tradutorlinux_gui)
```

---

## 5. Estrutura das Classes da Interface

### 5.1. `MainWindow` ([src/gui/main_window.hpp](file:///c:/Users/Admin/Desktop/ProjetosCode/Linux/TradutorLinux/src/gui/main_window.hpp))

```cpp
#pragma once

#include <QMainWindow>
#include <QListWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QProcess>

#include "tradutorlinux/catalog/app_catalog.hpp"

namespace tradutorlinux::gui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void on_search_text_changed(const QString& query);
    void on_app_selected(QListWidgetItem* item);
    void on_btn_choose_clicked();
    void on_btn_run_clicked();
    void on_btn_report_clicked();
    void on_btn_register_clicked();
    void on_process_output_ready();
    void on_process_finished(int exit_code, QProcess::ExitStatus status);

private:
    void setup_ui();
    void refresh_app_list();
    void append_log(const QString& text, const QString& color = "#e2e8f0");

    // Componentes visuais
    QLineEdit* search_input_{nullptr};
    QListWidget* app_list_{nullptr};
    QLineEdit* path_input_{nullptr};
    QTextEdit* log_output_{nullptr};
    QPushButton* btn_run_{nullptr};
    QPushButton* btn_report_{nullptr};
    QPushButton* btn_register_{nullptr};

    // Estado e Core
    catalog::AppCatalog catalog_;
    QProcess* current_process_{nullptr};
};

}  // namespace tradutorlinux::gui
```

---

## 6. Exemplos de Implementação das Funcionalidades

### A. Listar e Filtrar Aplicativos da Biblioteca em Tempo Real
```cpp
void MainWindow::refresh_app_list() {
    app_list_->clear();
    catalog_.load_from_file();

    for (const auto& app : catalog_.list_apps()) {
        auto* item = new QListWidgetItem(QString::fromStdString(app.name));
        item->setData(Qt::UserRole, QString::fromStdString(app.executable_path));
        item->setToolTip(QString::fromStdString(app.executable_path));
        app_list_->addItem(item);
    }
}

void MainWindow::on_search_text_changed(const QString& query) {
    for (int i = 0; i < app_list_->count(); ++i) {
        auto* item = app_list_->item(i);
        const bool matches = item->text().contains(query, Qt::CaseInsensitive);
        item->setHidden(!matches);
    }
}
```

### B. Escolher Executável Nativo do Sistema
```cpp
void MainWindow::on_btn_choose_clicked() {
    const QString file = QFileDialog::getOpenFileName(
        this,
        tr("Selecionar Executável Windows"),
        QDir::homePath(),
        tr("Executáveis Windows (*.exe *.EXE);;Todos os Arquivos (*)")
    );

    if (!file.isEmpty()) {
        path_input_->setText(file);
        append_log(tr("Arquivo selecionado: %1").arg(file), "#38bdf8");
    }
}
```

### C. Executar Programa em Segundo Plano sem Travar a Janela
```cpp
void MainWindow::on_btn_run_clicked() {
    const QString exe_path = path_input_->text().trimmed();
    if (exe_path.isEmpty()) return;

    if (current_process_ && current_process_->state() != QProcess::NotRunning) {
        append_log(tr("Aviso: um programa já está em execução."), "#f59e0b");
        return;
    }

    current_process_ = new QProcess(this);
    connect(current_process_, &QProcess::readyReadStandardOutput, this, &MainWindow::on_process_output_ready);
    connect(current_process_, &QProcess::readyReadStandardError, this, &MainWindow::on_process_output_ready);
    connect(current_process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::on_process_finished);

    append_log(tr("Iniciando execução: %1").arg(exe_path), "#22c55e");
    
    // Executa através do runner CLI do TradutorLinux
    current_process_->start("./tradutorlinux", QStringList() << "--trace" << exe_path);
}

void MainWindow::on_process_output_ready() {
    if (!current_process_) return;
    const QString stdout_data = current_process_->readAllStandardOutput();
    const QString stderr_data = current_process_->readAllStandardError();
    
    if (!stdout_data.isEmpty()) append_log(stdout_data, "#f8fafc");
    if (!stderr_data.isEmpty()) append_log(stderr_data, "#cbd5e1");
}
```

---

## 7. Roteiro de Migração

1. **Instalar pacotes Qt6** no ambiente Linux de desenvolvimento (`qt6-base-dev`).
2. **Atualizar `CMakeLists.txt`** com `find_package(Qt6 COMPONENTS Widgets REQUIRED)`.
3. **Criar a classe `MainWindow`** conectando os slots com as chamadas de `catalog::AppCatalog` e `prefix::initialize_prefix`.
4. **Validar a interface no Linux** com testes de busca, seleção de arquivos, cadastro na biblioteca e visualização de logs em tempo real.

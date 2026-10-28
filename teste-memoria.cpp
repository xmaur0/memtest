#include <windows.h>
#include <vector>
#include <thread>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <chrono>

#define ID_BTN_START 1
#define ID_STATIC_STATUS 2

HWND hStatusText;
HWND hBtnStart;
std::atomic<bool> testRunning(false);
std::atomic<unsigned long long> totalErrors(0);
std::atomic<int> completedThreads(0);
std::atomic<int> currentPass(0);
std::atomic<unsigned long long> totalBytesProcessed(0);

const int TOTAL_PASSES = 10; // Total de ciclos definidos para o teste completo

// Função da Thread de Teste Profundo por Núcleo
void ThreadDeepMemoryTest(size_t allocSize, std::atomic<unsigned long long>* errorCounter, std::atomic<int>* finishedThreads) {
    std::vector<char*> blocks;
    size_t blockSize = 64 * 1024 * 1024; // Blocos de 64 MB
    size_t allocated = 0;

    // Aloca a memória designada para este núcleo
    while (allocated < allocSize && testRunning) {
        size_t chunk = (allocSize - allocated < blockSize) ? (allocSize - allocated) : blockSize;
        char* ptr = (char*)VirtualAlloc(NULL, chunk, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!ptr) break;
        blocks.push_back(ptr);
        allocated += chunk;
    }

    if (allocated == 0) {
        (*finishedThreads)++;
        return;
    }

    int pass = 0;
    while (testRunning && pass < TOTAL_PASSES) {
        pass++;
        currentPass = pass;

        size_t bIdx = 0;
        for (char* block : blocks) {
            if (!testRunning) break;
            size_t currentSize = (bIdx == blocks.size() - 1) ? (allocated - (bIdx * blockSize)) : blockSize;

            // --- PADRÃO 1: 0xAA ---
            memset(block, 0xAA, currentSize);
            for (size_t i = 0; i < currentSize; i++) {
                if ((unsigned char)block[i] != 0xAA) {
                    (*errorCounter)++;
                }
            }
            totalBytesProcessed += currentSize;
            if (!testRunning) break;

            // --- PADRÃO 2: 0x55 ---
            memset(block, 0x55, currentSize);
            for (size_t i = 0; i < currentSize; i++) {
                if ((unsigned char)block[i] != 0x55) {
                    (*errorCounter)++;
                }
            }
            totalBytesProcessed += currentSize;
            if (!testRunning) break;

            // --- PADRÃO 3: 0x00 ---
            memset(block, 0x00, currentSize);
            for (size_t i = 0; i < currentSize; i++) {
                if ((unsigned char)block[i] != 0x00) {
                    (*errorCounter)++;
                }
            }
            totalBytesProcessed += currentSize;
            if (!testRunning) break;

            // --- PADRÃO 4: 0xFF ---
            memset(block, 0xFF, currentSize);
            for (size_t i = 0; i < currentSize; i++) {
                if ((unsigned char)block[i] != 0xFF) {
                    (*errorCounter)++;
                }
            }
            totalBytesProcessed += currentSize;

            bIdx++;
        }
    }

    // Liberação da memória ao encerrar
    for (char* block : blocks) {
        VirtualFree(block, 0, MEM_RELEASE);
    }

    (*finishedThreads)++;
}

void StartTest() {
    if (testRunning) return;
    testRunning = true;
    totalErrors = 0;
    completedThreads = 0;
    currentPass = 0;
    totalBytesProcessed = 0;

    EnableWindow(hBtnStart, FALSE);
    SetWindowTextW(hStatusText, L"Calculando RAM e preparando threads...");

    // Descobre os núcleos lógicos da CPU
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 2;

    // Consulta quanta RAM está livre e calcula 95% exatos com segurança
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    size_t targetBytes = 1024 * 1024 * 1024;
    if (GlobalMemoryStatusEx(&status)) {
        targetBytes = (size_t)(status.ullAvailPhys * 0.95);
    }

    size_t bytesPerThread = targetBytes / numThreads;
    unsigned long long bytesPerPass = (unsigned long long)targetBytes * 4;
    unsigned long long totalTestBytes = bytesPerPass * TOTAL_PASSES;

    std::wstringstream ss;
    ss << L"Iniciando em " << numThreads << L" núcleos...\nRAM alocada: " << (targetBytes / (1024 * 1024)) << L" MB.";
    SetWindowTextW(hStatusText, ss.str().c_str());

    // Dispara as threads de teste em paralelo
    for (unsigned int i = 0; i < numThreads; ++i) {
        std::thread(ThreadDeepMemoryTest, bytesPerThread, &totalErrors, &completedThreads).detach();
    }

    // Thread de monitoramento para calcular a % total e tempo estimado total
    std::thread([totalTestBytes, numThreads]() {
        auto startTime = std::chrono::steady_clock::now();

        while (completedThreads < (int)numThreads && testRunning) {
            Sleep(400);

            auto currentTime = std::chrono::steady_clock::now();
            double elapsedSeconds = std::chrono::duration<double>(currentTime - startTime).count();

            unsigned long long processed = totalBytesProcessed;
            if (processed > totalTestBytes) processed = totalTestBytes;

            // Porcentagem total do teste inteiro (0 a 100%)
            int percent = (int)(((double)processed / (double)totalTestBytes) * 100.0);
            if (percent > 100) percent = 100;

            int passVal = currentPass.load();

            std::wstringstream liveMsg;
            if (elapsedSeconds > 1.0 && processed > 0) {
                double bytesPerSec = (double)processed / elapsedSeconds;
                unsigned long long bytesRemaining = totalTestBytes - processed;
                int totalSecondsRemaining = (int)(bytesRemaining / bytesPerSec);

                int mins = totalSecondsRemaining / 60;
                int secs = totalSecondsRemaining % 60;

                liveMsg << L"Progresso Total: " << percent << L"% (Ciclo " << (passVal == 0 ? 1 : passVal) << L"/" << TOTAL_PASSES << L")\n"
                        << L"Tempo restante: " << mins << L"m " << secs << L"s | Erros: " << totalErrors;
            } else {
                liveMsg << L"Calculando tempo total estimado...\nAguarde...";
            }

            SetWindowTextW(hStatusText, liveMsg.str().c_str());
        }

        testRunning = false;
        
        // Mensagem final após a conclusão de todos os ciclos
        std::wstringstream finalMsg;
        unsigned long long errs = totalErrors;
        if (errs == 0) {
            finalMsg << L"Teste concluído 100%!\nSua memória RAM está ok, sem erros.";
        } else {
            finalMsg << L"Teste concluído.\nATENÇÃO: Foram encontrados " << errs << L" erros na RAM!";
        }
        SetWindowTextW(hStatusText, finalMsg.str().c_str());
        EnableWindow(hBtnStart, TRUE);
    }).detach();
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        hBtnStart = CreateWindowW(
            L"BUTTON", L"INICIAR TESTE PROFUNDO",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            30, 20, 325, 45,
            hwnd, (HMENU)ID_BTN_START, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL
        );

        hStatusText = CreateWindowW(
            L"STATIC", L"Clique em 'Iniciar' para testar a memória RAM.",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            30, 80, 325, 75,
            hwnd, (HMENU)ID_STATIC_STATUS, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL
        );
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BTN_START) {
            StartTest();
        }
        break;

    case WM_DESTROY:
        testRunning = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"DeepMemTestWin";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"Testador de Memória Profundo",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 210,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
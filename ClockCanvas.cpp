#include <windows.h>
#include <tchar.h>
#include <string>
#include <d2d1.h>
#include <ctime>

// 引入 Direct2D 連結庫
#pragma comment(lib, "d2d1.lib")

// --- 全域變數 ---
HINSTANCE hInst;
TCHAR szWindowClass[] = _T("MyScreenSaverClass");
TCHAR szTitle[] = _T("C++ Direct2D Clock Screen Saver");

// 滑鼠初始位置（全螢幕模式下用來判斷是否有明顯移動）
POINT initialMousePos = { -1, -1 };

// 屏保執行模式
enum ScreenSaverMode {
    MODE_SAVER,     // /s 全螢幕執行
    MODE_PREVIEW,   // /p 預覽模式
    MODE_CONFIG     // /c 設定模式
};

// --- Direct2D 全域介面指標 ---
ID2D1Factory* pD2DFactory = NULL;
ID2D1HwndRenderTarget* pRenderTarget = NULL;
ID2D1SolidColorBrush* pWhiteBrush = NULL;
ID2D1SolidColorBrush* pGrayBrush = NULL;
ID2D1SolidColorBrush* pAccentBrush = NULL; // 秒針螢光綠

// --- 函數宣告 ---
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void ParseCommandLine(LPSTR lpCmdLine, int& mode, HWND& parentHwnd);
void InitD2D(HWND hwnd);
void CleanD2D();
void RenderClock(HWND hwnd);

// === 程式入口 WinMain ===
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    hInst = hInstance;
    int mode = MODE_SAVER;
    HWND parentHwnd = NULL;

    // 1. 解析命令列參數
    ParseCommandLine(lpCmdLine, mode, parentHwnd);

    // 如果是設定模式，直接彈出提示並退出
    if (mode == MODE_CONFIG) {
        MessageBox(NULL, _T("此螢幕保護程式目前沒有可配置的設定。"), szTitle, MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // 2. 註冊視窗類別
    WNDCLASSEX wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH); // 靜態靜止期與擦除預設為黑色
    wcex.lpszClassName = szWindowClass;
    RegisterClassEx(&wcex);

    HWND hwnd = NULL;
    DWORD style = WS_POPUP;
    int x = 0, y = 0, width = 0, height = 0;

    // 3. 根據模式創建相對應的視窗骨架
    if (mode == MODE_PREVIEW && parentHwnd != NULL) {
        // 預覽模式：嵌入到系統設定的小視窗中
        style = WS_CHILD | WS_VISIBLE;
        RECT rect;
        GetClientRect(parentHwnd, &rect);
        width = rect.right;
        height = rect.bottom;
        hwnd = CreateWindowEx(0, szWindowClass, szTitle, style, 0, 0, width, height, parentHwnd, NULL, hInstance, NULL);
    }
    else {
        // 全螢幕屏保模式
        style = WS_POPUP | WS_VISIBLE;
        x = 0; y = 0;
        width = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
        hwnd = CreateWindowEx(WS_EX_TOPMOST, szWindowClass, szTitle, style, x, y, width, height, NULL, NULL, hInstance, NULL);

        ShowCursor(FALSE); // 隱藏滑鼠
    }

    if (!hwnd) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // 創建高頻鬧鐘控制動畫（約 16 毫秒一次，精準對齊 60 FPS）
    SetTimer(hwnd, 1, 16, NULL);

    // 4. 訊息迴圈
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (mode == MODE_SAVER) {
        ShowCursor(TRUE); // 退出時恢復滑鼠顯示
    }

    return (int)msg.wParam;
}

// === 命令列解析分流 ===
void ParseCommandLine(LPSTR lpCmdLine, int& mode, HWND& parentHwnd)
{
    std::string cmd(lpCmdLine);
    if (cmd.empty()) {
        mode = MODE_SAVER;
        return;
    }

    for (char& c : cmd) c = tolower(c);

    if (cmd.find("/s") != std::string::npos) {
        mode = MODE_SAVER;
    }
    else if (cmd.find("/c") != std::string::npos) {
        mode = MODE_CONFIG;
    }
    else if (cmd.find("/p") != std::string::npos) {
        mode = MODE_PREVIEW;
        size_t pos = cmd.find_last_of(" 0123456789");
        if (pos != std::string::npos) {
            std::string hwndStr = cmd.substr(pos);
            parentHwnd = (HWND)(ULONG_PTR)std::stoull(hwndStr);
        }
    }
}

// === Direct2D 顯示卡硬體資源初始化 ===
void InitD2D(HWND hwnd) {
    // 1. 創建 D2D 工廠
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &pD2DFactory);

    // 2. 測量當前視窗真實尺寸
    RECT rc;
    GetClientRect(hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    // 3. 綁定 HWND 建立 GPU 渲染目標（畫布）
    pD2DFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd, size),
        &pRenderTarget
    );

    // 4. 建立繪圖專用的固態顏色刷子
    pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &pWhiteBrush);
    pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::LightGray), &pGrayBrush);
    pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 1.0f, 0.5f, 1.0f), &pAccentBrush); // 螢光綠秒針
}

// === Direct2D 資源安全銷毀 ===
void CleanD2D() {
    if (pAccentBrush) { pAccentBrush->Release(); pAccentBrush = NULL; }
    if (pGrayBrush) { pGrayBrush->Release(); pGrayBrush = NULL; }
    if (pWhiteBrush) { pWhiteBrush->Release(); pWhiteBrush = NULL; }
    if (pRenderTarget) { pRenderTarget->Release(); pRenderTarget = NULL; }
    if (pD2DFactory) { pD2DFactory->Release(); pD2DFactory = NULL; }
}

// === Direct2D 圓盤時鐘渲染核心 ===
void RenderClock(HWND hwnd) {
    // 如果畫布還沒建立，立刻線上初始化
    if (!pRenderTarget) InitD2D(hwnd);

    pRenderTarget->BeginDraw();
    pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::Black)); // GPU 高速全螢幕塗黑

    // 1. 計算時鐘中心點與半徑
    D2D1_SIZE_F size = pRenderTarget->GetSize();
    D2D1_POINT_2F center = D2D1::Point2F(size.width / 2.0f, size.height / 2.0f);
    float radius = min(size.width, size.height) / 3.0f;

    // 2. 畫抗鋸齒精美外圓盤
    D2D1_ELLIPSE clockCircle = D2D1::Ellipse(center, radius, radius);
    pRenderTarget->DrawEllipse(clockCircle, pWhiteBrush, 4.0f); // 4像素粗的外白圈

    // ==========================================
    // 3. 獲取現實系統時間（升級為包含毫秒的 Windows 原生高精度時間）
    // ==========================================
    SYSTEMTIME st;
    GetLocalTime(&st); // 👈 完美取代舊的 time() 和 localtime_s()

    // 🌟 核心勻速數學公式 🌟
    // 將毫秒融入秒，將秒融入分，將分融入時，實現完全勻速、無縫絲滑流暢走動

    // 1. 勻速秒：當前秒數 + (當前毫秒 / 1000.0)
    float currentSeconds = st.wSecond + (st.wMilliseconds / 1000.0f);
    float secAngle = currentSeconds * 6.0f; // 每秒走 6 度

    // 2. 勻速分：當前分數 + (當前勻速秒 / 60.0)
    float currentMinutes = st.wMinute + (currentSeconds / 60.0f);
    float minAngle = currentMinutes * 6.0f; // 每分鐘走 6 度

    // 3. 勻速時：當前小時 + (當前勻速分 / 60.0)
    float currentHours = (st.wHour % 12) + (currentMinutes / 60.0f);
    float hourAngle = currentHours * 30.0f; // 每小時走 30 度
    // ==========================================

    // 儲存當前未旋轉的原始座標矩陣
    D2D1_MATRIX_3X2_F originalMatrix;
    pRenderTarget->GetTransform(&originalMatrix);

    // 4. 繪製粗時針（利用 D2D 幾何變換旋轉）
    pRenderTarget->SetTransform(D2D1::Matrix3x2F::Rotation(hourAngle, center));
    pRenderTarget->DrawLine(center, D2D1::Point2F(center.x, center.y - radius * 0.5f), pWhiteBrush, 8.0f);

    // 5. 繪製中分針
    pRenderTarget->SetTransform(D2D1::Matrix3x2F::Rotation(minAngle, center));
    pRenderTarget->DrawLine(center, D2D1::Point2F(center.x, center.y - radius * 0.75f), pGrayBrush, 5.0f);

    // 6. 繪製螢光綠細秒針
    pRenderTarget->SetTransform(D2D1::Matrix3x2F::Rotation(secAngle, center));
    pRenderTarget->DrawLine(center, D2D1::Point2F(center.x, center.y - radius * 0.85f), pAccentBrush, 2.0f);

    // 還原座標矩陣，確保後續繪圖不受影響
    pRenderTarget->SetTransform(originalMatrix);

    // 結束繪製
    HRESULT hr = pRenderTarget->EndDraw();
    // 💡 安全防護：萬一使用者在執行屏保時更改了螢幕解析度（Device Lost），GPU 畫布會失效
    if (hr == D2DERR_RECREATE_TARGET) {
        CleanD2D(); // 立刻清空，下一幀定時器觸發時會自動重新 InitD2D 重新適應新解析度！
    }
}

// === 大腦核心 視窗訊息處理器 ===
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    LONG_PTR style = GetWindowLongPtr(hWnd, GWL_STYLE);
    bool isPreview = (style & WS_CHILD) != 0;

    switch (message)
    {
    case WM_TIMER:
        // 16毫秒時間到，宣告整張畫板過期。最後參數填 FALSE 擋住系統粗暴擦除，交給 D2D1 完美覆蓋
        InvalidateRect(hWnd, NULL, FALSE);
        break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);

        // 調用 Direct2D 硬體加速渲染時鐘，原本的 GDI 雙緩衝與黑刷子全部退役！
        RenderClock(hWnd);

        EndPaint(hWnd, &ps);
    }
    break;

    // 以下事件在全螢幕屏保模式下觸發安全退出
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        if (!isPreview) {
            PostQuitMessage(0);
        }
        break;

    case WM_MOUSEMOVE:
        if (!isPreview) {
            int xPos = LOWORD(lParam);
            int yPos = HIWORD(lParam);

            if (initialMousePos.x == -1 && initialMousePos.y == -1) {
                initialMousePos.x = xPos;
                initialMousePos.y = yPos;
            }
            // 滑鼠大力搖晃超過 3 像素緩衝區，安全退出
            else if (abs(xPos - initialMousePos.x) > 3 || abs(yPos - initialMousePos.y) > 3) {
                PostQuitMessage(0);
            }
        }
        break;

    case WM_SIZE:
        // 💡 預覽小視窗可能會被系統縮放拉扯，解析度改變時需要銷毀 Direct2D 畫布以便重構
        CleanD2D();
        break;

    case WM_DESTROY:
        CleanD2D(); // 摧毀高階畫布，退還顯示卡記憶體
        KillTimer(hWnd, 1); // 砸碎高頻鬧鐘
        PostQuitMessage(0); // 宣告進程結束
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

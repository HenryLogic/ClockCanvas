#include <windows.h>
#include <tchar.h>
#include <string>
#include <d2d1.h>

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
ID2D1SolidColorBrush* pHourBrush = NULL;
ID2D1SolidColorBrush* pMinuteBrush = NULL;
ID2D1SolidColorBrush* pSecondBrush = NULL; // 秒針螢光綠

// --- 函數宣告 ---
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void ParseCommandLine(LPSTR lpCmdLine, int& mode, HWND& parentHwnd);
void InitD2D(HWND hwnd);
void CleanD2D();
void RenderClock(HWND hwnd);

// === 程式入口 WinMain（加入 SAL 批注修復警告二） ===
int APIENTRY WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
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
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &pD2DFactory);
    if (FAILED(hr)) return;

    // 2. 測量當前視窗真實尺寸
    RECT rc;
    GetClientRect(hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    // 3. 綁定 HWND 建立 GPU 渲染目標（畫布）
    hr = pD2DFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd, size),
        &pRenderTarget
    );

    // 🌟 安全檢查：如果建立成功，才繼續建立畫筆，修復警告一
    if (SUCCEEDED(hr) && pRenderTarget != NULL) {
        // ==========================================
        // 4. 建立繪圖專用的固態顏色刷子（升級為高級感現代藍色色調）
        // ==========================================
        // 時針專用：標準淺藍色 (RGB: 100, 200, 255)
        pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(100.0f / 255.0f, 200.0f / 255.0f, 255.0f / 255.0f, 1.0f), &pHourBrush);

        // 分針與中心環專用：極淺粉藍色 (RGB: 180, 230, 255) -> 視覺上更亮、浮在最上層
        pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(180.0f / 255.0f, 230.0f / 255.0f, 255.0f / 255.0f, 1.0f), &pMinuteBrush);

        // 秒針螢光綠保持不變
        pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 1.0f, 0.5f, 1.0f), &pSecondBrush);
    }
}

// === Direct2D 資源安全銷毀 ===
void CleanD2D() {
    if (pSecondBrush) { pSecondBrush->Release(); pSecondBrush = NULL; }
    if (pMinuteBrush) { pMinuteBrush->Release(); pMinuteBrush = NULL; }
    if (pHourBrush) { pHourBrush->Release(); pHourBrush = NULL; }
    if (pRenderTarget) { pRenderTarget->Release(); pRenderTarget = NULL; }
    if (pD2DFactory) { pD2DFactory->Release(); pD2DFactory = NULL; }
}

// === Direct2D 圓盤時鐘渲染核心 ===
void RenderClock(HWND hwnd) {
    // 如果畫布還沒建立，立刻線上初始化
    if (!pRenderTarget) InitD2D(hwnd);

    // 🌟 安全攔截：如果初始化後依然為 NULL（如顯卡驅動異常），則拒絕繪製，徹底消除警告一
    if (!pRenderTarget) return;

    pRenderTarget->BeginDraw();
    pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::Black)); // GPU 高速全螢幕塗黑

    // 1. 計算時鐘中心點與半徑
    D2D1_SIZE_F size = pRenderTarget->GetSize();
    D2D1_POINT_2F center = D2D1::Point2F(size.width / 2.0f, size.height / 2.0f);
    float radius = min(size.width, size.height) / 3.0f;

    // 2. 畫抗鋸齒精美外圓盤
    D2D1_ELLIPSE clockCircle = D2D1::Ellipse(center, radius, radius);
    pRenderTarget->DrawEllipse(clockCircle, pHourBrush, 4.0f); // 4像素粗的外白圈

    // ==========================================
    // 3. 獲取現實系統時間（包含毫秒的原生高精度時間）
    // ==========================================
    SYSTEMTIME st;
    GetLocalTime(&st);

    // 🌟 核心勻速數學公式 🌟
    float currentSeconds = st.wSecond + (st.wMilliseconds / 1000.0f);
    float secAngle = currentSeconds * 6.0f;

    float currentMinutes = st.wMinute + (currentSeconds / 60.0f);
    float minAngle = currentMinutes * 6.0f;

    float currentHours = (st.wHour % 12) + (currentMinutes / 60.0f);
    float hourAngle = currentHours * 30.0f;
    // ==========================================

    // 儲存當前未旋轉的原始座標矩陣（修改為正確的 3x2 矩陣）
    D2D1_MATRIX_3X2_F originalMatrix;
    pRenderTarget->GetTransform(&originalMatrix);

    // ==========================================
    // 4. 繪製精美時針（總長度變為秒針的 1/3，完美保持三七分比例）
    // ==========================================
    pRenderTarget->SetTransform(D2D1::Matrix3x2F::Rotation(hourAngle, center));

    // 🌟 核心比例修改：秒針長度是 radius * 0.85f，時針是它的 1/3
    float totalSecondLength = radius * 0.85f;
    float totalHourLength = totalSecondLength / 3.0f; // 👈 秒針的 1/3

    // 完美保持 3:7 的比例切分點
    float rectTopY = center.y - totalHourLength;          // 時針最尖端 (100%)
    float rectBottomY = center.y - (totalHourLength * 0.3f); // 圓角矩形底部起點 (遠離軸心 30% 處)

    // 【A 段：靠近旋轉軸的下半段實心線】占新總長度的 30%
    // 起點修正為 center.y - 10.0f，保持切齊中心圓環邊緣
    pRenderTarget->DrawLine(
        D2D1::Point2F(center.x, center.y - 10.0f),
        D2D1::Point2F(center.x, rectBottomY),
        pHourBrush,
        6.0f
    );

    // 【B 段：遠離旋轉軸的上半段 - 膠囊型空心圓角矩形】占新總長度的 70%
    D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(
        D2D1::RectF(
            center.x - 10.0f,  // 矩形左邊界
            rectTopY,          // 矩形上邊界（時針最尖端）
            center.x + 10.0f,  // 矩形右邊界
            rectBottomY        // 矩形下邊界
        ),
        10.0f, // 保持圓角半徑為寬度的一半，維持完美半圓弧（膠囊狀）
        10.0f
    );

    // 繪製空心膠囊形時針（粗細 6 像素）
    pRenderTarget->DrawRoundedRectangle(&roundedRect, pHourBrush, 6.0f);

    // ==========================================
    // 5. 繪製精美分針（🌟 升級為膠囊鏤空風：30% 實心線 + 70% 膠囊型空心圓角矩形）
    // ==========================================
    pRenderTarget->SetTransform(D2D1::Matrix3x2F::Rotation(minAngle, center));

    // 分針總長度保持為秒針的 2/3
    float totalMinLength = totalSecondLength * (2.0f / 3.0f);

    // 完美保持 15:85 的比例切分點
    float minRectTopY = center.y - totalMinLength;          // 分針最尖端 (100%)
    float minRectBottomY = center.y - (totalMinLength * 0.15f); // 分針圓角矩形底部起點 (遠離軸心 15% 處)

    // 【A 段：靠近旋轉軸的下半段實心線】占分針總長度的 30%
    // 起點同樣修正為 center.y - 10.0f，完美對齊中心圓環邊緣
    pRenderTarget->DrawLine(
        D2D1::Point2F(center.x, center.y - 10.0f),
        D2D1::Point2F(center.x, minRectBottomY),
        pMinuteBrush,
        6.0f // 6像素粗細
    );

    // 【B 段：遠離旋轉軸的上半段 - 苗條型空心圓角矩形】占分針總長度的 70%
    // 左右寬度為 20 像素（左 -10，右 +10）
    D2D1_ROUNDED_RECT minRoundedRect = D2D1::RoundedRect(
        D2D1::RectF(
            center.x - 10.0f,   // 矩形左邊界（往左拓寬 10 像素）
            minRectTopY,       // 矩形上邊界（分針最尖端）
            center.x + 10.0f,   // 矩形右邊界（往右拓寬 10 像素）
            minRectBottomY     // 矩形下邊界
        ),
        10.0f, // 🌟 核心幾何：圓角半徑改為寬度的一半（20 / 2 = 10），讓長分針窄邊也化為完美的半圓弧
        10.0f
    );

    // 繪製空心膠囊形分針（粗細 6 像素）
    pRenderTarget->DrawRoundedRectangle(&minRoundedRect, pMinuteBrush, 6.0f);

    // ==========================================
    // 6. 繪製螢光綠細秒針（長度為半徑的 0.85 倍）
    // ==========================================
    pRenderTarget->SetTransform(D2D1::Matrix3x2F::Rotation(secAngle, center));
    pRenderTarget->DrawLine(center, D2D1::Point2F(center.x, center.y - radius * 0.85f), pSecondBrush, 2.0f);

    // ==========================================
    // 🌟 繪製中心獨立圓環（還原矩陣，半徑 20 像素，粗細 6 像素）
    // ==========================================
    pRenderTarget->SetTransform(originalMatrix);
    float centerRingRadius = 10.0f;
    D2D1_ELLIPSE centerCircle = D2D1::Ellipse(center, centerRingRadius, centerRingRadius);
    pRenderTarget->DrawEllipse(centerCircle, pMinuteBrush, 6.0f);
    // ==========================================

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
#include <windows.h>
#include <tchar.h>
#include <string>

// 全域變數
HINSTANCE hInst;
TCHAR szWindowClass[] = _T("MyScreenSaverClass");
TCHAR szTitle[] = _T("C++ 2D Animation Screen Saver");

// 動態動畫變數（彈跳球）
int ballX = 100, ballY = 100;
int ballRadius = 30;
int speedX = 5, speedY = 5;

// 滑鼠初始位置（用來判斷是否有明顯移動）
POINT initialMousePos = { -1, -1 };

// 函數宣告
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void ParseCommandLine(LPSTR lpCmdLine, int& mode, HWND& parentHwnd);
void UpdateAnimation(HWND hwnd);

// 屏保執行模式
enum ScreenSaverMode {
    MODE_SAVER,     // /s 全螢幕執行
    MODE_PREVIEW,   // /p 預覽模式
    MODE_CONFIG     // /c 設定模式
};

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    hInst = hInstance;
    int mode = MODE_SAVER;
    HWND parentHwnd = NULL;

    // 1. 解析命令列參數
    ParseCommandLine(lpCmdLine, mode, parentHwnd);

    // 如果是設定模式，直接彈出提示並退出
    if (mode == MODE_CONFIG) {
        MessageBox(NULL, _T("此螢幕保護程式沒有可配置的設定。"), szTitle, MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // 2. 註冊視窗類別
    WNDCLASSEX wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH); // 背景黑色
    wcex.lpszClassName = szWindowClass;
    RegisterClassEx(&wcex);

    HWND hwnd = NULL;
    DWORD style = WS_POPUP;
    int x = 0, y = 0, width = 0, height = 0;

    // 3. 根據模式創建視窗
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

    // 創建定時器控制動畫（約每秒 60 幀）
    SetTimer(hwnd, 1, 16, NULL);

    // 4. 訊息迴圈
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (mode == MODE_SAVER) {
        ShowCursor(TRUE); // 恢復滑鼠
    }

    return (int)msg.wParam;
}

// 解析命令列參數的輔助函數
void ParseCommandLine(LPSTR lpCmdLine, int& mode, HWND& parentHwnd)
{
    std::string cmd(lpCmdLine);
    if (cmd.empty()) {
        mode = MODE_SAVER;
        return;
    }

    // 轉化為小寫方便處理
    for (char& c : cmd) c = tolower(c);

    if (cmd.find("/s") != std::string::npos) {
        mode = MODE_SAVER;
    }
    else if (cmd.find("/c") != std::string::npos) {
        mode = MODE_CONFIG;
    }
    else if (cmd.find("/p") != std::string::npos) {
        mode = MODE_PREVIEW;
        // 提取父視窗控制代碼（句柄）
        size_t pos = cmd.find_last_of(" 0123456789");
        if (pos != std::string::npos) {
            std::string hwndStr = cmd.substr(pos);
            parentHwnd = (HWND)(ULONG_PTR)std::stoull(hwndStr);
        }
    }
}

// 2D 動態邏輯
void UpdateAnimation(HWND hwnd)
{
    RECT rect;
    GetClientRect(hwnd, &rect);

    // 更新位置
    ballX += speedX;
    ballY += speedY;

    // 碰撞邊界檢測
    if (ballX - ballRadius < 0 || ballX + ballRadius > rect.right) {
        speedX = -speedX;
    }
    if (ballY - ballRadius < 0 || ballY + ballRadius > rect.bottom) {
        speedY = -speedY;
    }

    // 觸發視窗重繪
    InvalidateRect(hwnd, NULL, FALSE);
}

// 視窗訊息處理
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // 取得視窗樣式，判斷是否為預覽模式（預覽模式不響應滑鼠退出）
    LONG_PTR style = GetWindowLongPtr(hWnd, GWL_STYLE);
    bool isPreview = (style & WS_CHILD) != 0;

    switch (message)
    {
    case WM_TIMER:
        UpdateAnimation(hWnd);
        break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        // 使用雙緩衝（Double Buffering）防止螢幕閃爍
        RECT rect;
        GetClientRect(hWnd, &rect);
        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbmMem = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
        HGDIOBJ hOldBmp = SelectObject(hdcMem, hbmMem);

        // 1. 填滿黑色背景
        HBRUSH hBgBrush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(hdcMem, &rect, hBgBrush);
        DeleteObject(hBgBrush);

        // 2. 畫一個綠色的 2D 圓球
        HBRUSH hBallBrush = CreateSolidBrush(RGB(0, 255, 128));
        HGDIOBJ hOldBrush = SelectObject(hdcMem, hBallBrush);

        // 消除 GDI 畫圓外框線
        HPEN hNullPen = CreatePen(PS_NULL, 0, 0);
        HGDIOBJ hOldPen = SelectObject(hdcMem, hNullPen);

        Ellipse(hdcMem, ballX - ballRadius, ballY - ballRadius, ballX + ballRadius, ballY + ballRadius);

        // 清理 GDI 物件
        SelectObject(hdcMem, hOldPen);
        DeleteObject(hNullPen);
        SelectObject(hdcMem, hOldBrush);
        DeleteObject(hBallBrush);

        // 將記憶體緩衝區複製到螢幕
        BitBlt(hdc, 0, 0, rect.right, rect.bottom, hdcMem, 0, 0, SRCCOPY);

        SelectObject(hdcMem, hOldBmp);
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);

        EndPaint(hWnd, &ps);
    }
    break;

    // 以下事件在全螢幕模式下觸發退出
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

            // 記錄滑鼠初次移動的位置
            if (initialMousePos.x == -1 && initialMousePos.y == -1) {
                initialMousePos.x = xPos;
                initialMousePos.y = yPos;
            }
            // 如果滑鼠移動距離超過 3 像素，則判定使用者晃動滑鼠，退出屏保
            else if (abs(xPos - initialMousePos.x) > 3 || abs(yPos - initialMousePos.y) > 3) {
                PostQuitMessage(0);
            }
        }
        break;

    case WM_DESTROY:
        KillTimer(hWnd, 1);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

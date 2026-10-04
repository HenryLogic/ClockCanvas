#include <windows.h>
#include <tchar.h>
#include <string>
#include <d2d1.h>
#include <dwrite.h>

#pragma comment(lib, "dwrite.lib")
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
ID2D1SolidColorBrush* pSecondBrush = NULL;
ID2D1SolidColorBrush* pTickBrush = NULL;
ID2D1StrokeStyle* pRoundStrokeStyle = NULL; // 🌟 新增：圓潤端筆畫樣式指標
// --- DirectWrite 全域介面指標 🌟 ---
IDWriteFactory* pDWriteFactory = NULL;  // 文字工廠
IDWriteTextFormat* pTextFormat = NULL;     // 文字樣式格式

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
    if (FAILED(hr) || pD2DFactory == NULL) return; // 確保工廠絕對可用

    // 🌟 優化調整：工廠既然成功了，立刻建立「圓潤樣式」，不需要等畫布
    if (pRoundStrokeStyle == NULL) { // 避免重複建立
        pD2DFactory->CreateStrokeStyle(
            D2D1::StrokeStyleProperties(
                D2D1_CAP_STYLE_ROUND,
                D2D1_CAP_STYLE_ROUND,
                D2D1_CAP_STYLE_ROUND,
                D2D1_LINE_JOIN_ROUND,
                1.0f,
                D2D1_DASH_STYLE_SOLID,
                0.0f
            ),
            NULL,
            0,
            &pRoundStrokeStyle
        );
    }

    // 🌟 新增：向系統申請建立 DirectWrite 文字處理引擎
    if (pDWriteFactory == NULL) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&pDWriteFactory));
    }

    if (pDWriteFactory != NULL && pTextFormat == NULL) {
        // 建立現代無襯線體字型樣式：Arial，大小 16px，加粗，水平與垂直置中對齊
        pDWriteFactory->CreateTextFormat(
            L"Arial", NULL, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            24.0f, L"zh-tw", &pTextFormat
        );
        if (pTextFormat != NULL) {
            pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);       // 水平置中
            pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); // 垂直置中
        }
    }

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

    // 🌟 安全檢查：畫布建立成功，才建立依賴顯卡的畫刷
    if (SUCCEEDED(hr) && pRenderTarget != NULL) {
        // 時針專用：標準淺藍色
        pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(100.0f / 255.0f, 200.0f / 255.0f, 255.0f / 255.0f, 1.0f), &pHourBrush);

        // 分針與中心環專用：極淺粉藍色
        pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(180.0f / 255.0f, 230.0f / 255.0f, 255.0f / 255.0f, 1.0f), &pMinuteBrush);

        // 秒針白色
        pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &pSecondBrush);

        // 🌟 新增：調配中/小刻度專用深灰藍色 (RGB: 75, 90, 105) -> 低明度、極致內斂不刺眼
        pRenderTarget->CreateSolidColorBrush(
            D2D1::ColorF(75.0f / 255.0f, 90.0f / 255.0f, 105.0f / 255.0f, 1.0f),
            &pTickBrush
        );
    }
}

// === Direct2D 資源安全銷毀 ===
void CleanD2D() {
    if (pTextFormat) { pTextFormat->Release();     pTextFormat = NULL; }     // 🌟 釋放文字格式
    if (pDWriteFactory) { pDWriteFactory->Release();  pDWriteFactory = NULL; }  // 🌟 釋放文字工廠
    if (pRoundStrokeStyle) { pRoundStrokeStyle->Release(); pRoundStrokeStyle = NULL; } // 🌟 釋放畫筆樣式
    if (pTickBrush) { pTickBrush->Release();   pTickBrush = NULL; }
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

    // 儲存當前未旋轉的原始座標矩陣（修改為正確的 3x2 矩陣）
    D2D1_MATRIX_3X2_F originalMatrix;
    pRenderTarget->GetTransform(&originalMatrix);

    // 1. 計算時鐘中心點與半徑
    D2D1_SIZE_F size = pRenderTarget->GetSize();
    D2D1_POINT_2F center = D2D1::Point2F(size.width / 2.0f, size.height / 2.0f);
    float radius = min(size.width, size.height) / 3.0f;

    // 2. 畫抗鋸齒精美外圓盤
    D2D1_ELLIPSE clockCircle = D2D1::Ellipse(center, radius, radius);
    pRenderTarget->DrawEllipse(clockCircle, pHourBrush, 4.0f); // 4像素粗的外白圈

    // 🌟 核心比例修改：秒針長度是 radius * 0.85f，時針是它的 1/3
    float totalSecondLength = radius * 0.85f;
    float totalHourLength = totalSecondLength / 3.0f; // 👈 秒針的 1/3
    // 分針總長度保持為秒針的 2/3
    float totalMinLength = totalSecondLength * (2.0f / 3.0f);

    // ==========================================
    // 🌟 升級：繪製高密度高階小時刻度盤（大刻度為淺藍，中小刻度為純白，拉開視覺層次）
    // ==========================================
    float maxTickLength = totalHourLength * 0.25f; // 大刻度長度 (100%)

    // 總共需要繪製 48 根刻度線 (12 小時 * 4 等分)
    for (int i = 0; i < 48; ++i) {
        // 每相鄰兩根刻度線之間的角度剛好是 7.5 度
        float currentTickAngle = i * 7.5f;

        // 將畫布旋轉到當前刻度的角度
        pRenderTarget->SetTransform(D2D1::Matrix3x2F::Rotation(currentTickAngle, center));

        // 宣告臨時變數，用來存放當前刻度該用的【長度】與【畫筆刷子】
        float currentLen = 0.0f;
        ID2D1SolidColorBrush* pCurrentBrush = NULL; // 👈 動態刷子指標

        if (i % 4 == 0) {
            // 情況 A：整點【大刻度】
            currentLen = maxTickLength;
            pCurrentBrush = pSecondBrush; // 保持專屬的【白色】
        }
        else if (i % 4 == 2) {
            // 情況 B：正中間的【半點中刻度】
            currentLen = maxTickLength * 0.5f;
            pCurrentBrush = pTickBrush; // 🌟 核心修改：換成全新深灰藍刷子，完美隱退不刺眼
        }
        else {
            // 情況 C：兩旁的【等分小刻度】
            currentLen = maxTickLength * 0.25f;
            pCurrentBrush = pTickBrush; // 🌟 核心修改：換成全新深灰藍刷子，消除視覺噪點
        }

        // 幾何外沿切齊：所有刻度的終點 (End) 都死死固定在時針最外圈軌道上
        D2D1_POINT_2F tickEnd = D2D1::Point2F(center.x, center.y - totalHourLength);              // 外沿
        D2D1_POINT_2F tickStart = D2D1::Point2F(center.x, center.y - totalHourLength + currentLen); // 內沿（向心回縮）

        // 呼叫 DrawLine 繪製刻度線（套用動態切換的刷子）
        pRenderTarget->DrawLine(
            tickStart,
            tickEnd,
            pCurrentBrush,     // 👈 核心修改：傳入動態決定的淺藍或純白刷子
            2.5f,              // 2.5 像素粗細
            pRoundStrokeStyle  // 完美保持兩端半圓形的膠囊樣式
        );
    }

    // [ 緊跟在小時刻度盤的 for 迴圈大括號完結的下方，且在還原 originalMatrix 之前 ]

// ==========================================
// 🌟 新增：在內圈刻度盤內側，精準繪製 12, 3, 6, 9 四個極簡數字
// ==========================================
// 確保文字引擎初始化成功才放行
    if (pTextFormat != NULL) {
        // 確保座標軸此時處於完全端正的原始狀態，防止文字發生傾斜倒立
        pRenderTarget->SetTransform(originalMatrix);

        // 幾何計算：數字所在的內圈環形軌道半徑（剛好卡在刻度線內沿的下方，不產生重疊）
        float numberRadius = totalHourLength - (maxTickLength * 1.8f);

        // 定義 4 個數字的文字內容與它們的角度（12點是-90度，3點是0度，6點是90度，90度是180度）
        const wchar_t* numStr[] = { L"12", L"3", L"6", L"9" };
        float numAngles[] = { -90.0f, 0.0f, 90.0f, 180.0f }; // 以 3 點鐘方向為數學上的 0 度角

        for (int k = 0; k < 4; ++k) {
            // 將角度轉換為標準的弧度制 (弧度 = 角度 * PI / 180)
            float rad = numAngles[k] * 3.14159265f / 180.0f;

            // 完美推導出 4 個數字在錶盤上的端正圓弧中心點座標
            float numX = center.x + numberRadius * cos(rad);
            float numY = center.y + numberRadius * sin(rad);

            // 為文字挖一個 40x40 像素的「虛擬正方形中心排版盒子」
            D2D1_RECT_F textRect = D2D1::RectF(
                numX - 20.0f,
                numY - 20.0f,
                numX + 20.0f,
                numY + 20.0f
            );

            // 呼叫 DrawText 函數，將抗鋸齒的高清數字寫進格子裡
            pRenderTarget->DrawText(
                numStr[k],          // 文字內容（如 L"12"）
                wcslen(numStr[k]),  // 文字長度
                pTextFormat,        // 剛剛在 InitD2D 建立的 Arial 置中樣式
                textRect,           // 排版盒子
                pHourBrush          // 🌟 使用與時針完全相同的淺藍色，色系絕對和諧
            );
        }
    }
    // ==========================================

    // 鐵律：刻度盤畫完後，必須立刻把坐標矩陣還原
    pRenderTarget->SetTransform(originalMatrix);
    // ==========================================

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



    // ==========================================
    // 4. 繪製精美時針（總長度變為秒針的 1/3，完美保持三七分比例）
    // ==========================================
    pRenderTarget->SetTransform(D2D1::Matrix3x2F::Rotation(hourAngle, center));

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

    // 完美保持 15:85 的比例切分點
    float minRectTopY = center.y - totalMinLength;          // 分針最尖端 (100%)
    float minRectBottomY = center.y - (totalMinLength * 0.15f); // 分針圓角矩形底部起點 (遠離軸心 15% 處)

    // 【A 段：靠近旋轉軸的下半段實心線】占分針總長度的 30%
    // 起點同樣修正為 center.y - 10.0f，完美對齊中心圓環邊緣
    pRenderTarget->DrawLine(
        D2D1::Point2F(center.x, center.y - 10.0f),
        D2D1::Point2F(center.x, minRectBottomY),
        pMinuteBrush,
        6.0f // 6 像素粗細
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
    // 🌟 繪製中心獨立雙層圓環（還原矩陣，完美實現多層立體覆蓋視覺效果）
    // ==========================================
    pRenderTarget->SetTransform(originalMatrix);

    // 【第一層底座：外層極淺藍圓環】半徑 10 像素，粗細 6 像素
    float centerRingRadius1 = 10.0f;
    D2D1_ELLIPSE centerCircle1 = D2D1::Ellipse(center, centerRingRadius1, centerRingRadius1);
    pRenderTarget->DrawEllipse(centerCircle1, pMinuteBrush, 6.0f); // 與分針融為一體，蓋在時針上方

    // ==========================================
    // 6. 繪製純白細秒針與反向平衡尾巴（雙向發射，切齊內層白色圓環）
    // ==========================================
    pRenderTarget->SetTransform(D2D1::Matrix3x2F::Rotation(secAngle, center));

    // 【A 部分：正向秒針針身】起點往上挪移 8 像素，往正上方發射
    pRenderTarget->DrawLine(
        D2D1::Point2F(center.x, center.y - 8.0f), // 正上方 8 像素外沿
        D2D1::Point2F(center.x, center.y - totalSecondLength),
        pSecondBrush,
        4.0f, // 4 像素粗細
        pRoundStrokeStyle  // 👈 核心修改：套用圓潤樣式，讓秒針尖端化為完美半圓
    );

    // 🌟 【B 部分：新需求 - 反向配重尾巴】長度為秒針的 1/10，往正下方發射
    float tailLength = totalSecondLength * 0.1f; // 計算 1/10 秒針長度
    float tailStartY = center.y + 8.0f;         // 🌟 起點往下挪移 8 像素，切齊白色圓環下壁
    float tailEndY = tailStartY + tailLength;  // 尾巴的最遠端尖端

    pRenderTarget->DrawLine(
        D2D1::Point2F(center.x, tailStartY), // 從圓環下沿起點發射
        D2D1::Point2F(center.x, tailEndY),   // 延伸至 1/10 長度終點
        pSecondBrush, // 複用純白秒針刷子
        4.0f,         // 保持一體化的 4 像素粗細
        pRoundStrokeStyle  // 👈 核心修改：套用圓潤樣式，讓尾巴最遠端也化為完美半圓
    );

    // ==========================================
    // 🌟 史詩級升級：巨型聯動秒針計時盤（60大秒刻度 + 60中秒刻度 + 480小秒刻度 = 共600根航空級極細密刻度線）
    // ==========================================
    // 根據正向偏心公式，計算大秒盤在當前旋轉座標系下的圓心
    D2D1_POINT_2F bigSecondClockCenter = D2D1::Point2F(center.x, center.y + (5.0f * totalSecondLength));

    // 大秒盤半徑（秒針長度的 6 倍）
    float bigSecondClockRadius = totalSecondLength * 6.0f;

    // 🌟 核心修改：總共需要繪製 600 根極細密刻度線 (60 秒 * 10 等分)
    for (int m = 0; m < 600; ++m) {
        // 🌟 核心修改：每根刻度線之間的角度剛好是 0.6 度 (360度 / 600根)
        float bigSecondTickAngle = m * 0.6f;

        // 🌟 神級幾何矩陣複合：
        // 矩陣 1（右）：Matrix3x2F::Rotation(secAngle, center) -> 隨秒針整體公轉
        // 矩陣 2（中）：Matrix3x2F::Rotation(-secAngle, bigSecondClockCenter) -> 🌟 核心修改：圍繞大盤圓心反向自轉，把正確示數轉到針尖前方
        // 矩陣 3（左）：Matrix3x2F::Rotation(bigSecondTickAngle, bigSecondClockCenter) -> 沿著大盤圓周排版 600 根刻度
        pRenderTarget->SetTransform(
            D2D1::Matrix3x2F::Rotation(bigSecondTickAngle, bigSecondClockCenter) *
            D2D1::Matrix3x2F::Rotation(-secAngle, bigSecondClockCenter) * // 👈 增加這行自轉補償矩陣
            D2D1::Matrix3x2F::Rotation(secAngle, center)
        );

        float currentSecTickLen = 0.0f;
        ID2D1SolidColorBrush* pSecTickBrush = NULL;

        // 🌟 核心修改：利用 % 10 運算子智能判定 600 根線的 1秒/0.5秒 階梯主次層次
        if (m % 10 == 0) {
            // 情況 A：整除 10，代表這是【每秒整點大刻度】
            currentSecTickLen = maxTickLength;
            pSecTickBrush = pSecondBrush; // 複用全域變數：純白色刷子
        }
        else if (m % 10 == 5) {
            // 情況 B：餘數為 5，代表這是正中間的【0.5秒中刻度】（大刻度的二分之一）
            currentSecTickLen = maxTickLength * 0.5f;
            pSecTickBrush = pTickBrush;   // 複用全域變數：深灰藍色刷子
        }
        else {
            // 情況 C：其餘餘數，代表這是極細密的【0.1秒等分小刻度】（大刻度的四分之一）
            currentSecTickLen = maxTickLength * 0.25f;
            pSecTickBrush = pTickBrush;   // 複用全域變數：深灰藍色刷子
        }

        // 幾何外沿切齊：所有大盤刻度的終點都死死卡在大盤的外沿軌道上
        D2D1_POINT_2F bigSecTickEnd = D2D1::Point2F(bigSecondClockCenter.x, bigSecondClockCenter.y - bigSecondClockRadius);
        D2D1_POINT_2F bigSecTickStart = D2D1::Point2F(bigSecondClockCenter.x, bigSecondClockCenter.y - bigSecondClockRadius + currentSecTickLen);

        // 繪製高流暢抗鋸齒的圓潤膠囊型超高密度大秒盤刻度
        pRenderTarget->DrawLine(
            bigSecTickStart,
            bigSecTickEnd,
            pSecTickBrush,
            4.0f, // 4 像素粗細，維持極致的邊緣精細度
            pRoundStrokeStyle
        );

        // ==========================================
        // 🌟 核心修改：在大秒盤的 60 個大刻度內側，繪製【跟隨刻度方向傾斜】的數字示數
        // ==========================================
        if (m % 10 == 0 && pTextFormat != NULL) {
            // 🌟 直接沿用 transformMatrix！不做任何反向扭正，文字會自然隨著圓周傾斜旋轉

            // 幾何計算：在當前旋轉座標軸上，文字只需要沿著【正上方（Y軸負方向）】向心回縮即可
            float bigNumberRadius = bigSecondClockRadius - (maxTickLength * 1.85f);
            float textY = bigSecondClockCenter.y - bigNumberRadius;

            // 為 24px 大字體數字挖一個相對座標下的 50x50 虛擬排版盒子
            D2D1_RECT_F bigTextRect = D2D1::RectF(
                bigSecondClockCenter.x - 25.0f,
                textY - 25.0f,
                bigSecondClockCenter.x + 25.0f,
                textY + 25.0f
            );

            // 計算當前代表的秒數示數（12點鐘方向顯示 60）
            int secondDisplayValue = m / 10;
            if (secondDisplayValue == 0) secondDisplayValue = 60;

            wchar_t bigNumStr[16]; // 準備一個可以裝 16 個字的空盒子
            swprintf_s(
                bigNumStr,           // 1. 目的地：你要把文字印到哪一個字串盒子裡？ (Buffer)
                16,                  // 2. 盒子容量：這個盒子最多能裝幾個字？（防止溢位安全鎖，陣列可省略） (BufferCount)
                L"%d",               // 3. 格式化密碼：你想怎麼組裝？（%d 代表這是一個整數） (Format)
                secondDisplayValue   // 4. 真實數據：把哪一個變數的數值倒進 %d 的位置裡？ (Arguments)
            );

            // 呼叫 DrawText 將數字完美貼在環形發射軌道上
            pRenderTarget->DrawText(
                bigNumStr,
                (UINT32)wcslen(bigNumStr),
                pTextFormat,
                bigTextRect,
                pSecondBrush // 複用純白刷子
            );
        }
    }

    // 鐵律：秒針與大秒錶盤全部畫完，立刻把畫布座標矩陣強行扭回端正狀態！
    pRenderTarget->SetTransform(originalMatrix);
    // ==========================================

    // 【第二層頂蓋：內層純白圓環】半徑 6 像素，粗細 6 像素，覆蓋在最上層
    float centerRingRadius2 = 6.0f;
    D2D1_ELLIPSE centerCircle2 = D2D1::Ellipse(center, centerRingRadius2, centerRingRadius2);

    // 🌟 核心修改：直接傳入升級為純白色的 pSecondBrush，不再需要新建和釋放臨時資源！
    pRenderTarget->DrawEllipse(centerCircle2, pSecondBrush, 6.0f);
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
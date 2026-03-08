# 極速演算 - 期末報告完整講稿 (對應最新 14 頁版本)

**預估時間：** 7~8 分鐘

---

### Slide 1: Title (封面)
**(約 30 秒)**

**[開場]**
各位教授、同學大家好。我是 [您的名字]。

今天要為大家展示我的期末專案——**「極速演算：基於 OpenGL 的 F1 賽車圖形引擎與沈浸式跑道實作」**。

這不只是一個賽車遊戲，更是一個從零開始，完全不依賴 Unity 或 Unreal 等現成 **Game Engine**，而是使用 **Modern C++** 與 **OpenGL** 底層 API 一行一行打造出來的圖形引擎。我希望透過這個專案，展示如何實作出現代遊戲所需的渲染技術與物理系統。

---

### Slide 2: Project Overview (專案概覽)
**(約 40 秒)**

**[切換下一頁]**
首先是專案概覽。

我的核心目標非常明確：就是要打造一個同時具備 **「Photorealistic（照片級真實）」** 畫質與 **「Robust（穩健）」** 物理系統的模擬器。大家可以看到右邊這張圖，就是引擎實際運行的畫面。

為了達成這個目標，我實作了四個關鍵模組：
1.  **Rendering（渲染）：** 採用了 **Deferred Shading** 架構與 **PBR** 材質系統。
2.  **Physics（物理）：** 實作了 **Bicycle Model** 車輛動力學。
3.  **Environment（環境）：** 具備 **Dynamic Weather**、**Fog** 與光影變化。
4.  **Game Loop（遊戲邏輯）：** 包含完整的 HUD 與起跑邏輯。

---

### Slide 3: Technology Stack (技術堆疊)
**(約 40 秒)**

**[切換下一頁]**
在 **Technology Stack** 方面，我堅持使用「純手刻」的架構，以確保對效能的完全掌控。

* **Core Language：** 使用現代的 **C++17 Standard**。
* **Graphics API：** 採用 **OpenGL 4.3 Core Profile**，這讓我能完全掌控記憶體管理與渲染管線。
* **Libraries：** 僅使用最基礎的函式庫：**GLFW** 處理視窗跨平台，**GLM** 處理數學運算 (**Mathematics**)。
* **Asset Pipeline：** 模型部分使用 **tinygltf** 載入 GLB 格式，紋理則使用 **stb_image**。

---

### Slide 4: Core Rendering - Deferred Shading (延遲渲染)
**(約 50 秒)**

**[切換下一頁]**
接下來進入引擎的核心——渲染架構。

為了支援賽道場景中大量的動態光源，我放棄了傳統的 **Forward Rendering**，改採 **Deferred Shading (延遲渲染)**。

請看左邊這張流程圖：
1.  **Geometry Pass：** 在第一階段，我並不直接計算光照，而是將幾何資訊寫入多個 **Render Targets**，也就是我們常說的 **G-Buffer**。這包含了 **Position**、**Normal**、**Albedo** 以及 **PBR** 參數。
2.  **Lighting Pass：** 接著在第二階段，我才對螢幕上的每一個 **Pixel** 進行光照計算。

這樣做的好處是將 **Geometry Complexity** 與 **Lighting Calculation** 解耦 (**Decouple**)，即使場景中有數百個光源，效能也不會因為物體數量增加而顯著下降。

---

### Slide 5: Post-Processing (後處理)
**(約 40 秒)**

**[切換下一頁]**
有了 G-Buffer 的基礎後，我加入了 **Post-Processing** 流程來提升畫質。

* **HDR：** 我們使用 **GL_RGB16F** 浮點數紋理進行渲染，這允許亮度值超過 1.0，保留 **High Dynamic Range** 的細節。
* **Bloom：** 透過提取 **Threshold** 以上的高亮像素，利用 **Ping-Pong Buffers** 進行 **Gaussian Blur**，製造出車燈與霓虹燈的真實光暈感。
* **Gamma Correction：** 最後經過 **Tone Mapping** 與 **Gamma** 校正，確保輸出到螢幕的顏色是正確且自然的。

---

### Slide 6: Interface (互動介面)
**(約 60 秒)**

**[切換下一頁]**
除了畫面漂亮，良好的互動體驗也很重要。

在左邊的 **Control Features**：
我支援了手把的 **Analog Input**，這能做到鍵盤做不到的線性加速。玩家也可以隨時切換 **Cockpit View** 體驗第一人稱視角。
此外，我實作了**地形材質檢測**，當車輛駛入草地時，摩擦力會改變，導致車速變慢，懲罰偏離賽道的行為。

**[重點強調]**
在右邊的 **Visual Interface**，我們有純 OpenGL 繪製的儀表板與小地圖。
* **Visual HUD：** 包含儀表板資訊，我們實作了高精度的**計時系統 (Lap Timer)**，能即時顯示當前與最佳圈速。
* **Start Logic：** 我也實作了 **Traffic Lights** 倒數邏輯，若玩家偷跑，系統會比照 F1 規則 直接給予 **Jump Start Penalty (違規懲罰)**，大幅增加了競技真實感。
* **Mini-Map Logic：** 小地圖是利用 **Ortho Projection (正交投影)** 與座標映射公式，將 3D 世界座標轉換為 2D UI 座標。

---

### Slide 7: PBR Math (PBR 數學模型)
**(約 50 秒)**

**[切換下一頁]**
接著進入圖形學最關鍵的部分：**Physically Based Rendering (PBR)**。我們採用 **Cook-Torrance BRDF** 模型來模擬光線與表面的物理交互。

請看投影片左邊，這是 **Diffuse (漫反射)** 部分，我們使用 **Lambertian** 模型來模擬光線進入物體內部的散射。

右邊則是 **Specular (鏡面反射)** 部分，這由三個核心函數組成：
1.  **D (Distribution)：** 即 **GGX**，決定高光的形狀。
2.  **F (Fresnel)：** 使用 **Fresnel-Schlick**，計算不同視角下的反射率變化。
3.  **G (Geometry)：** 使用 **Smith** 函數，模擬微平面的 **Self-Shadowing (自遮蔽)**。

這些數學公式的整合，是讓賽車金屬材質看起來真實的關鍵。

---

### Slide 8: SSR & Wet Roads (螢幕空間反射)
**(約 50 秒)**

**[切換下一頁]**
將 PBR 理論應用於環境交互，就是 **SSR (Screen Space Reflections)**。

請看左邊這張截圖，這是針對 **Wet Roads (濕滑路面)** 實作的效果。
當材質的 **Roughness** 低於 0.1 時，我們會觸發 **SSR**。

演算法是在 **View Space** 中使用 **Ray-Marching** 技術追蹤光線，並比對 **Depth Buffer** 的交點。這讓積水路面能即時反射出周圍的霓虹燈與車輛，大幅增強了 **Immersion (沈浸感)**。

---

### Slide 9: Atmosphere (大氣效果)
**(約 30 秒)**

**[切換下一頁]**
為了強化氛圍，我加入了 **Atmospheric Effects**。

請看左邊的截圖，我們實現了 **Exponential Fog**。
在 **Fragment Shader** 中利用指數公式 `exp(-density * distance)` 混合場景顏色，創造出遠處的朦朧感與深度暗示。

另外還有 **Rain System**，雨滴是使用 **GL_LINES** 繪製的，利用位置循環的邏輯，在攝影機周圍創造出無限降雨的錯覺，既省效能又具備視覺說服力。

---

### Slide 10: Car Physics (車輛物理)
**(約 40 秒)**

**[切換下一頁]**
畫面之外，物理也是模擬器的靈魂。請看左圖這台紅牛賽車的模型。

我採用了 **Bicycle Model** 來簡化車輛動力學。將四輪簡化為前後各一輪，精確計算 **Steering Angle** 與 **Slip Angle**。

公式 $F = C \times \alpha$ 幫助我們計算出過彎時的 **Lateral Force**，讓車輛有甩尾與抓地力的物理回饋。同時，利用 **Barycentric Interpolation (重心插值)** 在賽道網格上計算精確高度，確保車輛緊貼地形起伏。

---

### Slide 11: Challenge - The "Puddle" Problem (水坑問題)
**(約 60 秒)**

**[切換下一頁]**
**(指向右邊的材質球圖片)**
在開發過程中，我遇到了一個有趣的技術挑戰，就是 **Rain-to-Sun Transition (雨轉晴的過渡)**。

起初，我使用 Global UV Tiling。這導致一個問題：當雨停了，如果我只調整全域的 **Roughness**，賽道狀態會變成二元對立：要嘛「全濕像鏡子」，要嘛「瞬間全乾」。

這無法呈現真實世界中**「路面乾了，但低窪處還有積水」**的細節。大家可以看到右邊這張圖，我們需要同一塊材質上同時存在「乾燥的粗糙面」與「積水的鏡面」。

解決方法是回到 Blender。我製作了一張 **Mix Map** (遮罩)。接著在 **Shader** 中使用這張遮罩來控制：讓大部分路面隨天氣變乾，但鎖定積水區域的 **Roughness** 保持在 0.05 的高反射狀態。最終成功實現了極具真實感的乾濕過渡效果。

---

### Slide 12: Engineering Challenges (工程挑戰)
**(約 50 秒)**

**[切換下一頁]**
除了圖形效果，我在載入這台賽車模型時也克服了幾個底層工程問題。

請看左邊這張圖，這是開發初期遇到的 **Rendering Artifacts**，車身出現了嚴重的破圖與透明化。

經過診斷，我解決了右邊列出的這四個主要問題：
1.  **UV Misalignment：** 貼圖反轉，透過 `1.0 - v` 翻轉座標解決。
2.  **Edge Artifacts：** 邊緣黑線，改用 **GL_CLAMP_TO_EDGE** 解決。
3.  **Invisible Mesh：** 這就是左圖透明的原因，是頂點順序錯誤導致背面剔除，我透過調整 **Cull Face** 設定修復。
4.  **Data Corruption：** 資料型態轉換錯誤，透過修正讀取邏輯解決。

---

### Slide 13: DEMO (實際演示)
**(時間：視演示長度而定，約 1-2 分鐘)**

**[切換下一頁]**

講了這麼多理論與實作細節，不如直接看實際運行的效果。
接下來是我的 **Live Demo**。

**(進行 Demo 操作...)**

---

### Slide 14: Thank You (結尾)
**(約 10 秒)**

**[切換下一頁]**

這是我今天的報告。
感謝各位的聆聽。
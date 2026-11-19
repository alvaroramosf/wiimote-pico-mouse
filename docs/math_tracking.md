# Diamond Tracking Mathematics (4 → 3 → 2 LEDs)

This document details the geometric pipeline implemented in [`src/diamond_tracker.c`](../src/diamond_tracker.c) and [`src/one_euro_filter.c`](../src/one_euro_filter.c) to map Wiimote PixArt IR detections into a jitter-free absolute USB HID pointer.

---

## 1. Camera Coordinate Frame & Accelerometer Roll

The PixArt IR camera outputs up to 4 valid blobs on a $1024 \times 768$ grid with optical center at $(512, 384)$. Raw coordinates are centered and oriented to match screen axes ($+u$ right, $+v$ down):

$$u = 512 - x_{\text{raw}}, \qquad v = y_{\text{raw}} - 384$$

Thus, the camera's optical axis (where the Wiimote is aimed) always sits at the origin $(u, v) = (0, 0)$.

The 10-bit ADXL330 accelerometer (zero-centered at `512` in report `0x33`) provides the gravity vector $(a_x, a_y, a_z)$, from which controller roll is computed:

$$\theta_{\text{roll}} = \operatorname{atan2}(a_x, a_z)$$

To rotate a camera vector $\vec{p} = (u, v)$ back into the upright TV plane, we apply:

$$R(\theta_{\text{roll}}) = \begin{bmatrix} \cos\theta_{\text{roll}} & -\sin\theta_{\text{roll}} \\ \sin\theta_{\text{roll}} & \cos\theta_{\text{roll}} \end{bmatrix}$$

---

## 2. 4-LED State: Full 2D Projective Homography

When all 4 diamond vertices are visible:

1. **Vertex Identification (`Top`, `Right`, `Bottom`, `Left`):**
   All $4! = 24$ permutations are evaluated, rejecting self-intersecting quads via $(P_R - P_L) \times (P_B - P_T) > 0$. Each candidate is scored by:
   - Alignment of centroid-relative vectors derotated by $R(\theta_{\text{roll}})$ against canonical directions $\{(0,-1), (1,0), (0,1), (-1,0)\}$.
   - Temporal continuity relative to the previous frame rotated by $-\Delta\theta_{\text{roll}}$.

2. **2D Homography:**
   We solve for the $3 \times 3$ projective matrix $H$ ($h_{22} = 1$) mapping the 4 camera vertices $P_k = (u_k, v_k)$ to canonical screen edge midpoints:

   | Vertex | Normalized Screen Target $(X_k, Y_k)$ |
   |---|---|
   | `Top` ($k=0$) | $(0.5,\; 0.0)$ |
   | `Right` ($k=1$) | $(1.0,\; 0.5)$ |
   | `Bottom` ($k=2$) | $(0.5,\; 1.0)$ |
   | `Left` ($k=3$) | $(0.0,\; 0.5)$ |

   Each correspondence yields two linear equations:

   $$u_k h_0 + v_k h_1 + h_2 - X_k u_k h_6 - X_k v_k h_7 = X_k$$
   $$u_k h_3 + v_k h_4 + h_5 - Y_k u_k h_6 - Y_k v_k h_7 = Y_k$$

   The $8 \times 8$ system is solved in double precision using Gaussian elimination with partial pivoting (after coordinate normalization for numerical stability). Evaluating $H$ at the optical center $(u, v) = (0, 0)$ yields directly:

   $$X_{\text{raw}} = \frac{h_2}{1} = h_2, \qquad Y_{\text{raw}} = \frac{h_5}{1} = h_5$$

3. **Live Geometry State Update:**
   Every 4-LED frame updates:
   - Diagonals $\vec{d}_H = P_R - P_L$, $\vec{d}_V = P_B - P_T$, lengths $L_H = \|\vec{d}_H\|$, $L_V = \|\vec{d}_V\|$, and aspect ratio $\rho = L_H / L_V$.
   - Perspective midpoint offset $\vec{\delta} = M_V - M_H = \alpha_H \vec{d}_H + \alpha_V \vec{d}_V$, where $M_H = \frac{1}{2}(P_L + P_R)$ and $M_V = \frac{1}{2}(P_T + P_B)$.
   - A drift-free reference template `ref_vertices[4]` and `ref_roll`.

---

## 3. 3-LED State: Perspective-Corrected 4th Vertex Reconstruction

When 1 LED exits the camera FOV (or is occluded), 3 blobs remain. The tracker evaluates all $4 \times 3! = 24$ assignments `(missing_slot, 3-point permutation)`:

- **Case A (`Top` or `Bottom` missing):**
  The horizontal diagonal $\vec{d}_H = P_R - P_L$ is complete. Estimating $\vec{d}_{V,\text{approx}} = (-d_{H,v}, d_{H,u}) / \rho$, the perspective-corrected vertical midpoint is:

  $$M_V = M_H + \alpha_H \vec{d}_H + \alpha_V \vec{d}_{V,\text{approx}}$$

  The missing vertex is reconstructed by reflection across $M_V$:

  $$P_{\text{missing}} = 2 M_V - P_{\text{opposite}}$$

- **Case B (`Left` or `Right` missing):**
  Symmetrically, from $\vec{d}_V = P_B - P_T$ and $\vec{d}_{H,\text{approx}} = (d_{V,v}, -d_{V,u}) \cdot \rho$:

  $$M_H = M_V - \alpha_H \vec{d}_{H,\text{approx}} - \alpha_V \vec{d}_V, \qquad P_{\text{missing}} = 2 M_H - P_{\text{opposite}}$$

The winning candidate minimizes translation-invariant pairwise edge error against the roll-updated previous frame, spatial continuity of the 3 visible dots, and prefers reconstructions outside the sensor rectangle. The 3 real + 1 virtual vertices are fed into the exact same 2D homography, preventing cursor jumps.

---

## 4. 2-LED State: 2D Similarity Fallback with Accelerometer Roll

On large TVs or at short distances, aiming into a corner can leave only 2 adjacent LEDs visible (e.g., `Top` + `Right`).

1. The stored `ref_vertices[4]` template (captured during the last 4-LED frame) is rotated to the live accelerometer roll via $\Delta\theta = \theta_{\text{roll}} - \theta_{\text{ref\_roll}}$, yielding $\vec{r}_k$.
2. All $4 \times 3 = 12$ ordered pairs $(a, b)$ are scored by comparing the observed vector $\vec{o} = P_1 - P_0$ against $\vec{e} = \vec{r}_b - \vec{r}_a$ in angle, scale, and temporal proximity to `last_vertices`.
3. For the winning pair $(a, b)$, the exact 2D similarity transform (uniform scale + rotation + translation) mapping $\vec{r}_a \mapsto P_0$ and $\vec{r}_b \mapsto P_1$ reconstructs all 4 vertices:

   $$c = \frac{\vec{o} \cdot \vec{e}}{\|\vec{e}\|^2}, \qquad d = \frac{o_v e_u - o_u e_v}{\|\vec{e}\|^2}$$
   $$V_k = \frac{P_0 + P_1}{2} + \begin{bmatrix} c & -d \\ d & c \end{bmatrix} \left(\vec{r}_k - \frac{\vec{r}_a + \vec{r}_b}{2}\right)$$

4. Because `ref_vertices` is never overwritten in 2-LED mode, zero geometric drift accumulates even when holding the pointer in a corner indefinitely.

---

## 5. 1-LED / 0-LED State

When fewer than 2 IR LEDs are visible, the pointer freezes smoothly at its last valid coordinates while all Wiimote buttons remain operational.

---

## 6. 2-Point Affine Calibration (Flash)

To compensate for bezel geometry and mounting tolerances, calibration mode (hold `+` and `-` for 3 s) locks the USB cursor to two known screen targets $(T_1 = (0.10, 0.10), \; T_2 = (0.90, 0.90))$ and records the raw homography outputs $(P_1, P_2)$ when `A` is pressed:

$$s_x = \frac{0.90 - 0.10}{P_{2,x} - P_{1,x}}, \qquad b_x = 0.10 - s_x P_{1,x}$$
$$X_{\text{cal}} = \operatorname{clamp}_{[0,1]}(s_x X_{\text{raw}} + b_x)$$

(and analogously for $Y$). All coefficients and reference diagonals are stored in Flash protected by a magic header and IEEE 802.3 CRC-32.

---

## 7. Adaptive 1-Euro Filter

To suppress natural hand tremor at $\ge 2.5\text{ m}$ without adding lag to fast movements, $(X_{\text{cal}}, Y_{\text{cal}})$ is filtered per axis (`src/one_euro_filter.c`):

1. Smoothed velocity with fixed cutoff $f_d = 1.0\text{ Hz}$:
   $$\hat{\dot{x}}_i = \operatorname{LPF}\left(\frac{x_i - \hat{x}_{i-1}}{\Delta t}, \; \alpha(\Delta t, f_d)\right)$$
2. Adaptive cutoff frequency:
   $$f_c = f_{c,\min} + \beta |\hat{\dot{x}}_i| \qquad (f_{c,\min} = 1.2\text{ Hz}, \; \beta = 2.5\text{ for normalized } [0,1]\text{ coordinates})$$
3. Filtered output:
   $$\hat{x}_i = \operatorname{LPF}(x_i, \; \alpha(\Delta t, f_c)), \qquad \alpha(\Delta t, f) = \frac{2\pi f \Delta t}{1 + 2\pi f \Delta t}$$

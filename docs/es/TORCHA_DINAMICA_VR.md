# Antorcha Dinámica de Mano (1H Club) para VR

## 1. Visión General
Esta característica transforma la antorcha o garrote de una mano (`clb`, `spc`) en una fuente de iluminación dinámica desacoplada del cuerpo que responde en tiempo real (1:1) a la mano y muñeca del jugador en Realidad Virtual, proyectando luz viva y parpadeante en mazmorras cerradas (Catacumbas, Cloacas, Tumbas de Tal Rasha) y permitiendo el combate cuerpo a cuerpo contundente (1H Club swing).

Todo el diseño se ciñe estrictamente a las **Golden Rules**:
- **Cero ediciones a `.d2s`:** No se crean objetos ni códigos de objeto nuevos (`weapons.txt` intacto).
- **Compatibilidad 100% con juego de escritorio:** Un personaje puede jugarse indistintamente en VR o en monitor plano sin corrupción de partida.
- **Solución canónica D2:** Los garrotes (`clb`, `spc`) cualifican como antorchas ardientes al imbuirse con fuego legítimo mediante mecánicas canónicas de Diablo II.

---

## 2. Fase 1: Tracking y Modelo Matemático
Implementado en [steamvr/torch_light.h](../../steamvr/torch_light.h):

- **Geometría y Offset de Agarre:**
  - `DefaultShaftOffset = {0.0f, 0.48f, -0.29f}`: punto emisor de la llama situado en la cabeza del garrote (~56 cm a lo largo del eje del garrote desde la muñeca hasta el extremo superior de la maza).
  - Función `ComputeTorchTip`: calcula la posición 3D exacta de la punta de la antorcha en el espacio de juego XR a partir de la pose del mando.
- **Inercia y Arrastre de la Llama:**
  - `ComputeFlameDirection`: calcula el vector de inclinación de la llama combinando la flotabilidad ascendente `{0, 1, 0}` con el arrastre por velocidad lineal del mando (`-velocity * dragFactor`).
- **Simulación de Fuego Orgánico (Flicker):**
  - `ComputeFlameFlickerWave`: combinación de armónicos senoidales primos (3.1 Hz, 7.3 Hz, 13.7 Hz, 23.9 Hz) que generan turbulencia continua en el rango `[-1.0, 1.0]` sin saltos artificiales.
  - `ComputeFlickerRadius`: modulación suave del radio de iluminación (`18.0f ± 6%`).
  - `ComputeFlickerColor`: modulación cromática de temperatura de fuego (rojo/dorado cálido).

---

## 3. Fase 2: Lógica de Cualificación e Inspección de Equipo
Implementado en [steamvr/torch_light.h](../../steamvr/torch_light.h), [steamvr/interactions.h](../../steamvr/interactions.h) y [vr/combat.cpp](../../vr/combat.cpp):

- `HasTorchInHand(Snapshot, hand, outFlags)`: verifica en cada fotograma si el conjunto activo de armas contiene un garrote válido con fuego en la mano indicada (0 = principal, 1 = secundaria).
- **Criterio Unificado de Daño de Fuego (`fireDamage > 0`):**
  - Leído directamente de las estadísticas nativas del motor D2R (`item_firemindam` / `item_firemaxdam`, stats 48 y 49) mediante `combat::EquippedFireDamage(location)`.
  - Cubre de forma robusta y automática:
    - Garrotes con rubíes engarzados (fragmentado hasta perfecto).
    - Garrotes con runa Ral engarzada (5-30 daño de fuego).
    - Garrote único canónico *Felloak* (`clb` nivel 3, con 6-8 daño de fuego inherente).
    - Afijos mágicos o raros de fuego (*Fiery*, *Flaming*, *Smoking*) y joyas engarzadas de fuego.
  - **Garrotes con engarces vacíos NO prenden:** Al tener 0 de daño de fuego, permanecen apagados.
  - **Otras gemas (zafiro, esmeralda, topacio, calavera) NO prenden:** Aportan daño de frío, veneno o rayo, por lo que el daño de fuego sigue siendo 0.
- **Hechizo *Enchant* de Hechicera Activo (`effects.Has(16)`):** Imbuye cualquier garrote con fuego vivo mientras dure el encantamiento.

---

## 4. Fase 3: Render Pipeline, Shader ReShade y Partículas 3D

### Foco de Luz en Tiempo Real (1:1 con el Mando)
Implementado en [vr/vrcam.cpp](../../vr/vrcam.cpp) y [reshade/D2R_DepthFog.fx](../../reshade/D2R_DepthFog.fx):
- **Tracking Directo de Mando OpenXR:** Se leen las coordenadas en tiempo real a 90/120 Hz de `D2RVR_Shared::rightHand` / `leftHand` y `rightRot` / `leftRot`.
- **Transformación a Coordenadas de Mundo D2R:**
  - El vector de la punta de la antorcha se proyecta en el espacio del mundo mediante los ejes de la cámara (`f.camR`, `f.camF`) escalados por `upm` (unidades de mundo por metro).
  - El foco de luz (`GameLightPos[0]`) se traslada exactamente con cada movimiento de la mano, giro de muñeca o desplazamiento del jugador, proyectando luz dinámica y sombras vivas sobre paredes, suelos y techos.
  - Si el tracking del mando se interrumpe temporalmente, conmuta suavemente al hueso de la animación (`skel::MeleeWeapon`) o al torso.

### Extracción de Texturas Oficiales D2R y Anclaje Físico 1:1
- **Pipeline de Texturas Auténticas (`sprites.ssp`):** Se extraen directamente desde los archivos CASC del juego instalado las texturas nativas de partículas de fuego de Diablo II: Resurrected (`firePlate`, `fireballPlate`, `softFlame`, `flameRamp`) a `d2rloader/plugins/d2r_vr_spell_sprites/sprites.ssp`. Esto reemplaza los puntos de aura procedurales básicos por fuego volumétrico auténtico de D2R.
- **Anclaje Físico al Mando Desacoplado de la Cabeza:** En `steamvr/transport.cpp`, `tipPos` y `tipRot` toman directamente la pose del mando físico OpenXR:
  $$\mathbf{P}_{\text{tip}} = \mathbf{P}_{\text{mando}} + \mathbf{R}_{\text{mando}} \cdot \mathbf{Offset}_{\text{shaft}}$$
  Se elimina por completo el acoplamiento con la inclinación o cabeceo de la cabeza del visor (`head.q`), garantizando que la llama permanezca anclada de forma fija y sólida a la porra.
- **Alineación de Base de Quads Físicos:** En `steamvr/elemental_hands_renderer.cpp`, los quads de la llama tienen su base física alineada a la cabeza del arma ($\mathbf{P} = \mathbf{P}_{\text{tip}} + \mathbf{up} \cdot \text{size}_y$), emergiendo hacia arriba por flotabilidad vertical sin hundirse en la empuñadura ni en la mano del jugador.
- **Composición Multicapa de la Llama:**
  - **Pilar Central Rugiente (Sprite 0):** Columna de fuego densa con desplazamiento vertical de texturas.
  - **Espirales Quiralmente Opuestas (Sprites 1 y 2):** Lenguas danzantes en contrarrotación sobre ejes horizontales ortogonales.
  - **Núcleo de Combustión Radiante (Sprite 4):** Foco incandescente blanco-dorado en el punto de combustión.
  - **Chispas y Ascuas Térmicas (Sprites 5-7):** Ascuas flotantes con física de ascenso vertical y dispersión.

### Sombreado Direccional por Normales y Atenuación Cuadrática en ReShade
- **Reconstrucción de Normales 3D:** En `reshade/D2R_DepthFog.fx`, a partir de las derivadas en espacio de pantalla de la posición reconstruida por profundidad ($\mathbf{dX} = \text{ddx}(\mathbf{worldP})$, $\mathbf{dY} = \text{ddy}(\mathbf{worldP})$), se calculan las normales de superficie $\mathbf{N} = \text{Normalize}(\mathbf{dX} \times \mathbf{dY})$.
- **Sombreado Direccional Lambertiano:** La luz emitida desde el foco de la antorcha $\mathbf{L} = \text{Normalize}(\mathbf{P}_{\text{antorcha}} - \mathbf{worldP})$ ilumina con contraste direccional:
  $$I_{\text{difusa}} = 0.25 + 0.75 \cdot \text{saturate}(\mathbf{N} \cdot \mathbf{L})$$
  Las caras orientadas hacia la antorcha reciben luz directa brillante mientras que las caras en sombra se mantienen oscuras (relación 4:1), haciendo visible instantáneamente el foco de luz al mover la mano.
- **Atenuación Cuadrática Inversa Realista:** Sustituida la anterior distribución plana por atenuación cuadrática con el cuadrado de la distancia:
  $$\text{atten} = \frac{1.0}{1.0 + 0.35 d + 0.04 d^2} \cdot \text{Falloff}(d / R)$$
  Las superficies cercanas a la antorcha (suelo, pilares y muros a 1-3 metros) forman un foco luminoso nítido y brillante que barre las superficies conforme el jugador mueve el brazo o gira la muñeca.
- **Slot 0 Reservado e Inmunidad a Distancia:** El slot 0 del array `GameLightPos[16]` y `GameLightCol[16]` pertenece a la antorcha del jugador con intensidad $1.0$ (`col[3] = 1.0f`), garantizando activación prioritaria en techos y paredes.
- **Configuración (`d2r_pcvr.ini`):**
  - `[torch] enabled=1`: Activa la iluminación de antorcha en VR.
  - `[torch] radius=18.0`: Alcance base de la luz en unidades de mundo.
  - `[torch] flicker=1`: Activa el parpadeo senoidal multifrecuencia.
  - `[torch] warmth=0.65`: Ajuste de temperatura cromática hacia naranja fuego.

---

## 5. Menú de Desarrollador (Dev Scenario)
- Opción **"Torch kit"** en la página **EQUIPMENT** del menú DEV (Choice ID 41).
- Genera en el inventario un garrote de una mano (`clb` - Club normal, nivel 1) para pruebas inmediatas en Blood Moor y Catacumbas.

---

## 6. Validación Automatizada
- Test nativo `torch-light-test`: valida cálculos de offset, inclinación por velocidad, límites del parpadeo, conversión espacial, reconocimiento de códigos, reglas de cualificación por daño de fuego, escala de radio de shader, y empaque del slot 0.
- Tests `elemental-hands-test` y `elemental-hands-render-test`: validan renderizado de partículas, animación y shaders GPU.
- Test `dev-scenarios-test`: valida la inclusión del kit de antorcha en la página de equipamiento.

---

## 7. Problemas Conocidos (Pendiente de Corrección)

- **Desincronización al equipar la antorcha en la mano izquierda / dual-wielding (detectado en pruebas con Bárbaro):**
  - Al equipar el garrote antorcha en la mano derecha (ranura principal), la posición, rotación, luz y partículas se sincronizan de forma óptima con la punta del arma física en VR.
  - Al equipar la antorcha en la mano izquierda (ranura secundaria / off-hand, probado en configuración de doble empuñadura con Bárbaro), el modelo del arma en VR permanece renderizado en la mano derecha, mientras que las partículas de llama flotan en el vacío en la posición espacial que le correspondería al lado derecho. Pendiente de auditar el mapeo de huesos y accesorios en `skel::MeleeWeapon` para el slot secundario.
- **Extinción del objeto al caer al suelo:**
  - Si el jugador suelta o tira la antorcha al suelo, la llama y la luz dinámica se extinguen. Los objetos arrojados en el escenario existen como entidades/props estáticos de botín nativos del juego y actualmente no mantienen activo el emisor de luz ni el volumen de partículas en el suelo.

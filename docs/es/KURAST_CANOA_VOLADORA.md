# Investigación de la canoa voladora en Muelles de Kurast

## Descripción del problema

En los Muelles de Kurast (Acto III), los jugadores en Realidad Virtual en primera persona
observaban una barca/canoa flotando a gran altura en el cielo (~50 metros de elevación)
hacia el noroeste, meciéndose rítmicamente con el vaivén del agua. En el juego clásico en
pantalla plana con cámara isométrica cenital fija, este objeto nunca era visible.

## Análisis de causa raíz

La inspección del preset nativo de la ciudad (`data/hd/env/preset/act3/docktown/docktown3.json`)
reveló:

- La entidad 4719 posee el ID `3418903870` y el nombre `prefab014`.
- Instancia el prefab `data/hd/env/model/act3/docktown/act3_docktown_boats/prefab/pf_canoe01_07_bob.json`,
  que empaqueta `canoe01.model`, provisiones (sacos, esterilla, remo) y un componente
  `WaveBobSimpleComponent` que aplica oscilación sinusoidal simulando olas.
- Su componente `TransformDefinitionComponent` está posicionado exactamente en:
  $$\begin{cases} X = 0.0 \\ Y = 50.0 \\ Z = 0.0 \end{cases}$$
- En contraste, todas las embarcaciones legítimas de Muelles de Kurast están ancladas
  en el agua a cotas entre $Y = -3.5$ y $Y = -0.5$.
- La posición $(0.0, 50.0, 0.0)$ representa un elemento de prueba o descuido de los
  diseñadores de niveles en el origen de coordenadas a 50 metros de altura.
- En el juego plano original, la cámara isométrica fija a ~55° apuntando hacia abajo
  y centrada en el jugador ($X \approx 200..350$, $Z \approx 200..350$) deja la cota
  $Y=50$ completamente fuera del cono de visión (*frustum*).
- En VR, la libertad total de orientación en 360° permite mirar hacia arriba y hacia el
  noroeste, haciendo visible la canoa meciéndose en mitad del cielo.

## Solución implementada

Siguiendo el estándar de arquitectura del proyecto para corregir problemas de geometría nativa
(como las alcantarillas de Kurast o el cielo del Santuario Arcano):

1. Se creó [`steamvr/prepare_kurast_dock.py`](../../steamvr/prepare_kurast_dock.py), que extrae
   `docktown3.json` de los archivos del juego del usuario mediante `CascLib`.
2. Detecta y elimina exclusivamente la entidad defectuosa (ID `3418903870` / prefab de canoa
   con $Y \ge 25.0$), preservando intactas las 4.988 entidades legítimas de la ciudad.
3. Despliega el preset corregido en `mods/D2R-PCVR/D2R-PCVR.mpq/data/hd/env/preset/act3/docktown/docktown3.json`
   junto con su recibo de verificación (`pcvr-kurast-dock.json`).
4. Se integró en [`steamvr/install.py`](../../steamvr/install.py) para su despliegue automático durante la instalación.
5. Se añadieron pruebas unitarias completas con datos sintéticos en [`steamvr/prepare_kurast_dock_test.py`](../../steamvr/prepare_kurast_dock_test.py).


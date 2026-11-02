# DaddySerch

Medidor de audio VST3 con visualizadores en tiempo real y una mascota animada estilo dibujos de los 90.
**No modifica el audio**, solo lo muestra. Ponlo al final de tu canal Master.

## Módulos
| Módulo | Qué hace |
|---|---|
| DADDY (mascota) | Duerme en silencio. Con sonido despierta, abre la boca según el nivel y lanza estrellas, notas y ondas con cada golpe |
| SPECTRUM | Espectro FFT (20 Hz – 20 kHz, escala logarítmica) |
| LEVELS / LUFS | Picos, RMS, LUFS momentáneo, corto plazo e integrado (ITU-R BS.1770) |
| OSCILLOSCOPE | Osciloscopio con disparo estable |
| STEREOMETER | Goniómetro (Mid/Side) + correlación de fase |
| SPECTROGRAM | Espectrograma desplazable |

**Controles:** doble clic en un módulo = pantalla completa (otro doble clic vuelve). Clic derecho = menú
(reiniciar LUFS integrado, elegir módulo). La ventana se puede redimensionar.

---

## Opción A (la más fácil): que GitHub lo compile, sin instalar nada
1. Crea una cuenta gratis en https://github.com y un repositorio nuevo llamado `DaddySerch`.
2. Sube **todo el contenido** de esta carpeta (incluida la carpeta oculta `.github`). En la web: *Add file → Upload files*.
   Si la carpeta `.github` no se sube arrastrando, crea el archivo `.github/workflows/build.yml` desde la web
   (*Add file → Create new file*, escribe esa ruta y pega el contenido).
3. Ve a la pestaña **Actions**. Se ejecuta "Build DaddySerch" solo (tarda ~10 min la primera vez).
4. Al terminar, entra en la ejecución y descarga el artefacto **DaddySerch-windows-latest** (o macOS).
5. Dentro encontrarás `VST3/DaddySerch.vst3`.

## Opción B: compilar en tu PC (Windows)
Necesitas: Git, CMake y Visual Studio (Build Tools con "Desarrollo de escritorio con C++").
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target DaddySerch_VST3
```
Resultado: `build\DaddySerch_artefacts\Release\VST3\DaddySerch.vst3`

---

## Instalación
**Windows:** copia `DaddySerch.vst3` a `C:\Program Files\Common Files\VST3\`
**macOS:** copia a `~/Library/Audio/Plug-Ins/VST3/` y ejecuta en Terminal:
`xattr -cr ~/Library/Audio/Plug-Ins/VST3/DaddySerch.vst3` (el plugin no está firmado).

**FL Studio:** Options → Manage plugins → *Find installed plugins*. Después, en el Mixer, abre un slot del canal **Master** y elige DaddySerch.
**Ableton Live:** Preferences → Plug-ins → activa *Use VST3 System Folders* → *Rescan*. Arrastra DaddySerch al final de la cadena de la pista **Master**.

## Licencia
Usa el framework JUCE (AGPLv3 o licencia comercial). Para uso personal o de código abierto no hay problema;
si lo vendes sin liberar el código, necesitas una licencia comercial de JUCE.

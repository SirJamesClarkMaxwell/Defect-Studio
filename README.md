# DefectsStudio

DefectsStudio to desktopowa aplikacja do podgladu, edycji i przygotowania struktur atomowych VASP (POSCAR/CONTCAR), tworzona jako narzedzie typu "Blender-like" dla pracy ze strukturami materialowymi.

Projekt jest rozwijany na Windows, w C++23, z wykorzystaniem OpenGL i Dear ImGui (Docking).

## Czym ta aplikacja jest

DefectsStudio laczy:
- wizualizacje 3D atomow,
- narzedzia selekcji i transformacji obiektow sceny,
- import/export danych VASP,
- panelowy edytor sceny (Outliner, Properties, Tools, Settings),
- workflow zblizony do DCC (krotkie skroty, gizmo, menu kontekstowe, 3D cursor).

Docelowo ma to byc lekkie, szybkie srodowisko do przygotowania i inspekcji struktur defektowych bez potrzeby uruchamiania ciezkich pakietow 3D.

## Jak zbudowac i uruchomic

Wymagania:
- Windows 10/11
- Visual Studio 2022 z toolsetem C++ (v143)
- PowerShell 5+ (dla skryptow setup/build)

Kroki (wariant zalecany):
1. Uruchom `scripts/Setup.bat`.
2. Otworz `DefectsStudio.sln` w Visual Studio 2022.
3. Zbuduj konfiguracje `Debug|x64` albo `Release|x64`.
4. Ustaw `DefectsStudio` jako Startup Project i uruchom (F5 lub Ctrl+F5).

Szybka weryfikacja skryptami:
- `scripts/Verify-Build.bat` - sprawdza build Debug/Release.
- `scripts/Verify-Build-And-Run.bat` - build + uruchomienie aplikacji.

## Co juz jest

Najwazniejsze gotowe elementy:
- system builda oparty o Premake5 + projekt VS2022,
- architektura warstwowa (`Core`, `Layers`, `Renderer`, `IO`, `DataModel`, `Editor`, `UI`),
- renderer OpenGL z viewportem offscreen,
- kamera orbitalna i nawigacja viewportu,
- import/export POSCAR/CONTCAR,
- renderowanie instancyjne atomow,
- selekcja kliknieciem i box-select,
- transformacje przez gizmo i modalny workflow (`G` + osie),
- obiekty sceny (m.in. empties), grupowanie i kolekcje,
- panele Scene Outliner + Object Properties + Tools,
- wydzielone okno `SettingsPanel`,
- menu `Shift+A`, usuwanie obiektow (`Delete`), menu pie, menu kontekstowe viewportu,
- utrwalanie ustawien UI i czesci ustawien sceny w plikach konfiguracyjnych.

## Co jest planowane

Najblizsze etapy rozwoju:
- T06: Bonds and measurements
   - automatyczne tworzenie wiazan,
   - konfiguracja cutoffow (globalnych i per para pierwiastkow),
   - narzedzia odleglosci i katow.
- T07: Dalsze dopracowanie UX i paneli
   - trwalsza persystencja ustawien osi i rendererowych,
   - dopracowanie narzedzi logowania/profilowania i domyslnych konfiguracji.
- T08: Offscreen render/F12 pipeline
   - render do PNG/JPG w zadanej rozdzielczosci.
- T09: Volumetrics MVP
   - parsery CHG/CHGCAR/PARCHG,
   - kontrola izopowierzchni.
- T10: testy, probki i dokumentacja
   - testy parserow i round-tripy,
   - lepszy zestaw plikow przykladowych,
   - dalsza dokumentacja uzytkowa.

Szczegolowy, aktualny stan prac znajduje sie w `TODO.md`.

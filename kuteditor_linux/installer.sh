#!/bin/bash

CONFIG_DIR="$HOME/.config/kut"
CONFIG_FILE="$CONFIG_DIR/installer.conf"

# Default values
OPT_WHISPER="ON"
OPT_KUTPOD="ON"

# Load previous configuration if exists
if [ -f "$CONFIG_FILE" ]; then
    source "$CONFIG_FILE"
fi

cat << 'BANNER'
  _  __     _   _____    _ _ _             
 | |/ /   _| |_| ____|__| (_) |_ ___  _ __ 
 | ' / | | | __|  _| / _` | | __/ _ \| '__|
 | . \ |_| | |_| |__| (_| | | || (_) | |   
 |_|\_\__,_|\__|_____\__,_|_|\__\___/|_|   
                                           
BANNER
echo ""

# Ask about Whisper
read -p "¿Instalar con soporte para Whisper (transcripción)? [Y/n] (Actual: $OPT_WHISPER): " input_whisper
if [[ "$input_whisper" =~ ^[Nn]$ ]]; then
    OPT_WHISPER="OFF"
elif [[ "$input_whisper" =~ ^[Yy]$ || -z "$input_whisper" ]]; then
    if [[ -z "$input_whisper" ]]; then
        # keep previous value if empty, unless it's empty in config too
        [ -z "$OPT_WHISPER" ] && OPT_WHISPER="ON"
    else
        OPT_WHISPER="ON"
    fi
fi

# Ask about KutPod
read -p "¿Instalar con soporte para KutPod (biblioteca online)? [Y/n] (Actual: $OPT_KUTPOD): " input_kutpod
if [[ "$input_kutpod" =~ ^[Nn]$ ]]; then
    OPT_KUTPOD="OFF"
elif [[ "$input_kutpod" =~ ^[Yy]$ || -z "$input_kutpod" ]]; then
    if [[ -z "$input_kutpod" ]]; then
        [ -z "$OPT_KUTPOD" ] && OPT_KUTPOD="ON"
    else
        OPT_KUTPOD="ON"
    fi
fi

# Save configuration
mkdir -p "$CONFIG_DIR"
cat <<EOF > "$CONFIG_FILE"
OPT_WHISPER="$OPT_WHISPER"
OPT_KUTPOD="$OPT_KUTPOD"
EOF

echo ""
echo "Opciones seleccionadas:"
echo "- Whisper: $OPT_WHISPER"
echo "- KutPod:  $OPT_KUTPOD"
echo "Guardado en $CONFIG_FILE"
echo "Iniciando compilación..."
echo ""

# Sincronizar la versión de PKGBUILD con CMakeLists.txt antes de compilar
if [ -f "PKGBUILD" ] && [ -f "CMakeLists.txt" ]; then
    PKGBUILD_VER=$(grep -E "^pkgver=" PKGBUILD | cut -d= -f2)
    PKGBUILD_VER=$(echo "$PKGBUILD_VER" | tr -d '"' | tr -d "'")
    if [ -n "$PKGBUILD_VER" ]; then
        echo "Sincronizando versión de PKGBUILD ($PKGBUILD_VER) con CMakeLists.txt..."
        sed -i -E "s/(project\(kuteditor VERSION )[0-9.]+( LANGUAGES C CXX)/\1$PKGBUILD_VER\2/g" CMakeLists.txt
    else
        echo "Advertencia: No se pudo detectar la versión en PKGBUILD."
    fi
fi

# Limitar hilos de compilación para evitar quedarse sin RAM (aprox. 3GB por hilo)
NUM_JOBS=2
if [ -f /proc/meminfo ]; then
    TOTAL_RAM=$(grep MemTotal /proc/meminfo | awk '{print $2}')
    TOTAL_RAM_GB=$((TOTAL_RAM / 1024 / 1024))
    NUM_JOBS=$((TOTAL_RAM_GB / 3))
    [ $NUM_JOBS -lt 1 ] && NUM_JOBS=1
    if command -v nproc &>/dev/null; then
        MAX_JOBS=$(nproc)
        [ $NUM_JOBS -gt $MAX_JOBS ] && NUM_JOBS=$MAX_JOBS
    fi
fi
echo "Limitando compilación a $NUM_JOBS hilos en paralelo para evitar falta de memoria..."
echo ""

# Detect OS
OS_ID=""
OS_LIKE=""
if [ -f /etc/os-release ]; then
    source /etc/os-release
    OS_ID=$ID
    OS_LIKE=$ID_LIKE
fi

is_arch() {
    [[ "$OS_ID" == "arch" || "$OS_LIKE" == *"arch"* || "$OS_ID" == "manjaro" || "$OS_ID" == "endeavouros" || "$OS_ID" == "cachyos" ]]
}

is_debian() {
    [[ "$OS_ID" == "debian" || "$OS_LIKE" == *"debian"* || "$OS_ID" == "ubuntu" || "$OS_ID" == "linuxmint" ]]
}

is_fedora() {
    [[ "$OS_ID" == "fedora" || "$OS_LIKE" == *"rhel"* || "$OS_ID" == "centos" || "$OS_ID" == "opensuse"* ]]
}

if is_arch; then
    echo "Detectado Arch Linux. Creando e instalando paquete nativo con makepkg..."
    echo ""
    
    # Verificar conflicto de PipeWire y JACK en Arch
    if pacman -Qi pipewire &>/dev/null; then
        if ! pacman -Qi pipewire-jack &>/dev/null; then
            echo -e "\e[33m[ADVERTENCIA] Se detectó PipeWire en el sistema, pero 'pipewire-jack' no está instalado.\e[0m"
            echo -e "KutEditor necesita 'pipewire-jack' para redirigir la API de JACK a PipeWire."
            echo -e "Si usas 'jack2' tradicional, la reproducción/grabación no funcionará porque"
            echo -e "PipeWire ya está controlando la tarjeta de sonido."
            echo ""
            read -p "¿Deseas instalar 'pipewire-jack' ahora con pacman? (Esto reemplazará 'jack2') [Y/n]: " inst_pw_jack
            if [[ "$inst_pw_jack" =~ ^[Yy]$ || -z "$inst_pw_jack" ]]; then
                sudo pacman -S pipewire-jack
            fi
            echo ""
        fi
    fi

    # Modify PKGBUILD dynamically to inject options
    if [ -f "PKGBUILD" ]; then
        sed -i "s/-DHAVE_WHISPER=[A-Z]*/-DHAVE_WHISPER=$OPT_WHISPER/" PKGBUILD
        
        # Insert HAVE_KUTPOD if it's missing, or update it
        if grep -q "DHAVE_KUTPOD" PKGBUILD; then
            sed -i "s/-DHAVE_KUTPOD=[A-Z]*/-DHAVE_KUTPOD=$OPT_KUTPOD/" PKGBUILD
        else
            sed -i "s/-DHAVE_WHISPER=$OPT_WHISPER/-DHAVE_WHISPER=$OPT_WHISPER \\\\\n        -DHAVE_KUTPOD=$OPT_KUTPOD/" PKGBUILD
        fi
        
        # Limpiar residuos de compilaciones anteriores para forzar una compilación limpia
        rm -rf pkg/ src/build/ build/ pkg/ *.pkg.tar.*
        
        # Build and install using pacman (NUNCA usar -c porque borra la carpeta src/ local)
        MAKEFLAGS="-j$NUM_JOBS" makepkg -si
        
        if [ $? -eq 0 ]; then
            echo ""
            echo "¡Kut Editor empaquetado e instalado exitosamente con Pacman!"
            # Limpiar el archivo .pkg.tar.zst y los directorios build que quedaron atrás
            rm -rf src/build/ build/ pkg/ *.pkg.tar.*
        else
            echo "Error al crear el paquete de Arch Linux."
        fi
    else
        echo "Error: PKGBUILD no encontrado."
    fi

else
    # Clear MOC cache to prevent AUTOMOC bugs when toggling HAVE_KUTPOD
    rm -rf build/kuteditor_autogen build/CMakeFiles/kuteditor.dir/kuteditor_autogen
    
    # Run standard CMake
    cmake -S . -B build -DHAVE_WHISPER=$OPT_WHISPER -DHAVE_KUTPOD=$OPT_KUTPOD
    if [ $? -ne 0 ]; then
        echo "Error en configuración de CMake."
        exit 1
    fi
    
    cmake --build build -j $NUM_JOBS
    if [ $? -ne 0 ]; then
        echo "Error en compilación."
        exit 1
    fi
    
    if is_debian; then
        echo "Detectado Debian/Ubuntu. Empaquetando DEB nativo..."
        cd build && cpack -G DEB
        if [ $? -eq 0 ]; then
            echo "Paquete .deb creado exitosamente en la carpeta 'build/'."
            echo "Puedes instalarlo con: sudo dpkg -i build/kuteditor-*.deb"
        fi
        cd ..
    elif is_fedora; then
        echo "Detectado Fedora/RedHat. Empaquetando RPM nativo..."
        cd build && cpack -G RPM
        if [ $? -eq 0 ]; then
            echo "Paquete .rpm creado exitosamente en la carpeta 'build/'."
            echo "Puedes instalarlo con: sudo rpm -i build/kuteditor-*.rpm"
        fi
        cd ..
    else
        echo ""
        echo "¡Kut Editor compilado exitosamente!"
        echo "Puedes ejecutarlo desde: ./build/kuteditor"
        echo "(Opcionalmente puedes instalarlo manualmente usando 'sudo cmake --install build')"
    fi
fi

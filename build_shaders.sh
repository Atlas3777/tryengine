#!/bin/bash

# Скрипт для компиляции шейдеров
echo "Компиляция шейдеров..."

# Компиляция фрагментного шейдера
glslc engine/graphics/assets/shaders/fragment.frag -o engine/graphics/assets/shaders/fragment.spv
if [ $? -ne 0 ]; then
    echo "Ошибка при компиляции фрагментного шейдера"
    exit 1
fi

# Компиляция вершинного шейдера
glslc engine/graphics/assets/shaders/vertex.vert -o engine/graphics/assets/shaders/vertex.spv
if [ $? -ne 0 ]; then
    echo "Ошибка при компиляции вершинного шейдера"
    exit 1
fi

echo "Шейдеры успешно скомпилированы!"

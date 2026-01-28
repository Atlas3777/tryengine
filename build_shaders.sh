#!/bin/bash

# Скрипт для компиляции шейдеров
echo "Компиляция шейдеров..."

# Компиляция фрагментного шейдера
glslc shaders/fragment.frag -o shaders/fragment.spv
if [ $? -ne 0 ]; then
    echo "Ошибка при компиляции фрагментного шейдера"
    exit 1
fi

# Компиляция вершинного шейдера
glslc shaders/vertex.vert -o shaders/vertex.spv
if [ $? -ne 0 ]; then
    echo "Ошибка при компиляции вершинного шейдера"
    exit 1
fi

echo "Шейдеры успешно скомпилированы!"

#pragma once

#include <QIcon>

// Простые векторные иконки пунктов дерева (рисуются в QPixmap 16x16,
// без внешних ресурсов). Каждый тип узла отличается и формой, и цветом.
namespace TreeIcons {
QIcon forwardZones();   // Зоны прямого просмотра (синий глобус)
QIcon reverseZones();   // Зоны обратного просмотра (зелёный глобус со стрелкой)
QIcon forwarderZones(); // Зоны пересылки (оранжевый глобус со стрелкой)
QIcon zone();           // Зона (цилиндр)
QIcon scope();          // Область зоны (слои)
QIcon host();           // Узел с записями (монитор)
QIcon policies();       // Правила (воронка)
QIcon subnets();        // Подсети клиентов (узлы сети)
} // namespace TreeIcons

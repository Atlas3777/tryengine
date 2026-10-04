#pragma once

namespace tryengine::graphics {

class RenderGraphBuilder;
class RGExecuteContext;
struct CollectContext;

// Интерфейс рендер-пасса. Объект живёт внутри RenderGraph (rg.AddPass<T>(...)),
// поэтому держит свои шейдеры, настройки, хэндлы ресурсов и покадровые данные.
//
// Жизненный цикл:
//   Collect  - каждый кадр, ДО Compile: собрать CPU-данные, импортировать внешние ресурсы.
//   Setup    - только при (пере)компиляции графа: объявить ресурсы (Create/Read/Write).
//              Покадровые данные здесь НЕ читать и в лямбды не захватывать.
//   Execute  - каждый кадр: записать команды. Хэндлы из Setup здесь валидны.
//
// Пасс без потребителей своих результатов вырезается (culling) - если он пишет
// "наружу" (readback, present, ImGui), вызови builder.MarkSideEffect() в Setup.
class IRenderPass {
public:
    virtual ~IRenderPass() = default;

    [[nodiscard]] virtual const char* Name() const = 0;

    // Участвует ли пасс в графе. Смена значения между кадрами => граф пересоберётся.
    [[nodiscard]] virtual bool IsActive() const { return true; }

    virtual void Collect(CollectContext& ctx) {}
    virtual void Setup(RenderGraphBuilder& builder) = 0;
    virtual void Execute(RGExecuteContext& ctx) = 0;
};

}  // namespace tryengine::graphics
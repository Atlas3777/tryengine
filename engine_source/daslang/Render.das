options gen2

require tryengine/Types
require tryRenderer
require daslib/decs

[export]
def GetLight(var arr : SpawnPointVector) {
    // В daScript типы из ManagedVectorAnnotation ведут себя как структуры,
    // переданные по ссылке. Ключевое слово 'var' делает эту ссылку мутабельной.

    // Создаем именно SpawnPoint через ваш C++ биндинг
    var aa = make_spawn_point(1, 4f, 5f)
    var bb = make_spawn_point(2, 200f, 17f)


    // Пушим SpawnPoint в ваш std::vector<SpawnPoint>
    push(arr, aa)
    push(arr, bb)
}

// [export]
// def GetRenderObj(var arr :){

// }


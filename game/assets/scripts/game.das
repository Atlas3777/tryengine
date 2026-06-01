options gen2

require gameq
require math

struct Particle {
    pos : float3
    vel : float3
    life : float
}

enum State {
    alive
    dead
}

let GRAVITY = float3(0.0, -9.8, 0.0)

var particles : array<Particle>

def update_particle(var p : Particle; dt : float) : State {
    p.vel += GRAVITY * dt
    p.pos += p.vel * dt
    p.life -= dt
    if (p.life > 0.0) {
        return State.alive
    }
    return State.dead
}

[export]
def test() {
    print("Hello from daslasdfdsfng!\n")
    gameq::test2()

    let dt = 0.016
    for (p in particles) {
        update_particle(p, dt)
    }
    print("particles: {length(particles)}\n")
}

[export]
def testt(dt: float) {
    print("test!\n")
    print("aaaa")
}

[init]
def setup {
    for (i in range(100)) {
        particles |> push(Particle(pos=float3(0), vel=float3(0, 10.0, 0), life=5.0))
    }
}
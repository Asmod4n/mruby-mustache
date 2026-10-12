plugins {
    kotlin("jvm") version "2.2.20"
    id("me.champeau.jmh") version "0.7.3"
}
repositories { mavenCentral() }
dependencies {
    implementation("org.jetbrains.kotlinx:kotlinx-html-jvm:0.12.0")
}
kotlin { jvmToolchain(21) }
jmh {
    warmupIterations.set(5)
    iterations.set(10)
    fork.set(1)
    timeOnIteration.set("1s")
    warmup.set("1s")
    benchmarkMode.set(listOf("avgt"))
    timeUnit.set("ns")
    resultFormat.set("JSON")
}

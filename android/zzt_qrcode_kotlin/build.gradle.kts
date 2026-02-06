import org.jetbrains.kotlin.gradle.dsl.JvmTarget
import org.jetbrains.kotlin.gradle.dsl.KotlinVersion

plugins {
    alias(libs.plugins.android.library)
}

android {
    namespace = "xyz.zhuzeitou.qrcode"
    compileSdk {
        version = release(36)
    }

    defaultConfig {
        minSdk = 24

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        consumerProguardFiles("consumer-rules.pro")
        externalNativeBuild {
            cmake {
                arguments("-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON", "-DBUILD_ANDROID_JNI=ON")
            }
        }

        ndk {
            ndkVersion = "29.0.14206865"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )

            externalNativeBuild {
                cmake {
                    arguments("-DCMAKE_BUILD_TYPE=Release")
                }
            }
        }
    }
    externalNativeBuild {
        cmake {
            path("../../CMakeLists.txt")
            version = "4.1.2"
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    kotlin {
        compilerOptions {
            languageVersion = KotlinVersion.KOTLIN_2_0
            jvmTarget = JvmTarget.JVM_11
        }
    }
}

dependencies {
    implementation(libs.kotlinx.coroutines.android)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
}
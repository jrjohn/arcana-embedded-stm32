// Jenkinsfile — multibranch pipeline for arcana-embedded-stm32 (STM32 firmware build)
// Adapted from legacy stm32-app-pipeline single-branch job.
//
// Key differences from the legacy XML-embedded script:
//   * `checkout scm` (no hardcoded branch=main)          — supports every branch + every PR
//   * `pollSCM` trigger removed                          — multibranch + GitHub webhook drive triggers
//   * `dir("${env.PROJECTS_DIR}/arcana-embedded-stm32")` removed — multibranch uses workspace root
//   * `Arch Qube Metrics` gated `when { branch 'main' }`  — main-only metrics push
//   * SonarQube gets pullrequest.* params on PRs          — PR-decoration in Sonar UI
//   * Post-build messages include branch/PR context

pipeline {
    agent any

    options {
        timeout(time: 30, unit: 'MINUTES')
        buildDiscarder(logRotator(numToKeepStr: '10', artifactNumToKeepStr: '1'))
        disableConcurrentBuilds()
        timestamps()
    }

    environment {
        APP_NAME     = "stm32-app"
        VERSION      = "1.0.0"
        PROJECT_NAME = "stm32-app"
    }

    stages {
        stage("Checkout") {
            steps {
                checkout scm
                sh 'git log -1 --oneline'
                script {
                    echo "Branch: ${env.BRANCH_NAME ?: 'unknown'}"
                    echo "PR: ${env.CHANGE_ID ?: 'no'} (target: ${env.CHANGE_TARGET ?: 'n/a'})"
                }
            }
        }

        stage("Firmware Build") {
            steps {
                sh "VERSION=${VERSION} docker compose -f docker-compose.ci.yml build"
            }
        }

        stage("Unit Tests + Coverage") {
            steps {
                sh "docker build -f Dockerfile.test -t stm32-test:ci . 2>&1"
                sh "docker create --name stm32-cov stm32-test:ci"
                sh "docker cp stm32-cov:/workspace/coverage.info . 2>/dev/null || echo 'No coverage.info'"
                sh "docker cp stm32-cov:/workspace/coverage.xml  . 2>/dev/null || echo 'No coverage.xml'"
                sh "docker rm stm32-cov || true"
                sh "ls -lh coverage.info 2>/dev/null || true"
            }
        }

        stage("SonarQube Analysis") {
            steps {
                catchError(buildResult: 'SUCCESS', stageResult: 'UNSTABLE') {
                    script {
                        def prArgs = env.CHANGE_ID ? " -Dsonar.pullrequest.key=${env.CHANGE_ID} -Dsonar.pullrequest.branch=${env.BRANCH_NAME} -Dsonar.pullrequest.base=${env.CHANGE_TARGET}" : ''
                        sh """docker run --rm --network devops_default \\
                            -v \$(pwd):/usr/src \\
                            sonarsource/sonar-scanner-cli:11 \\
                            sonar-scanner \\
                            -Dsonar.host.url=http://sonarqube:9000/sonarqube \\
                            -Dsonar.token=squ_5ce2319b9d8ca2b1db4e0f5bdf36b34249561f18 \\
                            -Dsonar.projectKey=${PROJECT_NAME}${prArgs}"""
                    }
                }
            }
        }

        stage("Extract Artifacts") {
            steps {
                sh "docker create --name ${APP_NAME}-out stm32-app-build:${VERSION} 2>/dev/null || true"
                sh "rm -rf /tmp/${APP_NAME}-firmware && docker cp ${APP_NAME}-out:/artifacts/ /tmp/${APP_NAME}-firmware/ 2>/dev/null || echo No artifacts dir"
                sh "docker rm ${APP_NAME}-out 2>/dev/null || true"
                sh "ls -la /tmp/${APP_NAME}-firmware/ 2>/dev/null || echo No firmware output"
            }
        }

        stage("Cleanup Old Images") {
            steps {
                sh "docker image prune -f || true"
                sh "docker images stm32-test --format '{{.ID}} {{.CreatedAt}}' | sort -r | tail -n +4 | awk '{print \$1}' | xargs docker rmi -f 2>/dev/null || true"
            }
        }

        stage("Architecture Qube") {
            steps {
                catchError(buildResult: 'SUCCESS', stageResult: 'UNSTABLE') {
                    sh """
                        mkdir -p arch-qube-reports
                        docker run --rm \\
                            --network devops_default \\
                            -v \$(pwd):/project \\
                            -v \$(pwd)/arch-qube-reports:/output \\
                            arcana.boo/arcana/arch-qube:latest scan /project \\
                            --framework stm32 --no-ai \\
                            --ci --format json,markdown \\
                            -o /output --threshold 90 || true
                    """
                }
            }
        }

        stage("Arch Qube Metrics") {
            // Metrics script writes to shared report dir, only run for main.
            when { branch 'main' }
            steps {
                catchError(buildResult: 'SUCCESS', stageResult: 'SUCCESS') {
                    sh "bash /data/projects/_scripts/arch-qube-metrics.sh \$(pwd) arcana-embedded-stm32 || true"
                }
            }
        }
    }

    post {
        success { echo "Pipeline SUCCESS - embedded-stm32 branch=${env.BRANCH_NAME ?: '?'} pr=${env.CHANGE_ID ?: 'no'}" }
        failure { echo "Pipeline FAILED - branch=${env.BRANCH_NAME ?: '?'} pr=${env.CHANGE_ID ?: 'no'}" }
        always  { echo "Build number ${BUILD_NUMBER} done" }
    }
}

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
                // Defensive cleanup — prior aborted build may have left this container,
                // and `docker create --name` fails on collision (#2 hit this 2026-05-23).
                // One shell block so the container name can carry the branch: a fixed "stm32-cov"
                // collided whenever two branches built at once (one's `docker rm -f` removed the
                // other's container). Same fail-fast behaviour as the former separate sh steps.
                sh '''
                    COV_CTR="stm32-cov-$(printf '%s' "${BRANCH_NAME}-${BUILD_NUMBER}" | tr -c 'A-Za-z0-9_.-' '-')"
                    docker rm -f "$COV_CTR" 2>/dev/null || true
                    docker create --name "$COV_CTR" stm32-test:ci
                    docker cp "$COV_CTR":/workspace/coverage.info . 2>/dev/null || echo 'No coverage.info'
                    docker cp "$COV_CTR":/workspace/coverage.xml  . 2>/dev/null || echo 'No coverage.xml'
                    docker rm "$COV_CTR" || true
                '''
                sh "ls -lh coverage.info 2>/dev/null || true"
            }
        }

        stage("SonarQube Analysis") {
            steps {
                withSonarQubeEnv('SonarQube') {
                    // Native scanner reads sonar-project.properties (sources, cxx config,
                    // coverage.xml). withSonarQubeEnv supplies SONAR_HOST_URL + the auth
                    // token, so no host.url/token is hardcoded here.
                    sh "sonar-scanner -Dsonar.projectKey=stm32-app -Dsonar.scm.disabled=true"
                    // Community Build has no webhook waitForQualityGate(); poll the CE task
                    // named in report-task.txt, then read the quality-gate status by analysisId.
                    sh '''
                        set -e
                        TOKEN="${SONAR_AUTH_TOKEN:-$SONAR_TOKEN}"
                        RT=.scannerwork/report-task.txt
                        [ -f "$RT" ] || { echo "report-task.txt missing"; exit 1; }
                        CE_TASK_ID=$(grep '^ceTaskId=' "$RT" | cut -d= -f2-)
                        ANALYSIS_ID=""
                        for i in $(seq 1 60); do
                            RESP=$(curl -s -u "$TOKEN:" "$SONAR_HOST_URL/api/ce/task?id=$CE_TASK_ID")
                            ST=$(echo "$RESP" | grep -o '"status":"[A-Z_]*"' | head -1 | cut -d'"' -f4)
                            echo "  CE status: ${ST:-?} (try $i)"
                            if [ "$ST" = "SUCCESS" ]; then ANALYSIS_ID=$(echo "$RESP" | grep -o '"analysisId":"[^"]*"' | head -1 | cut -d'"' -f4); break;
                            elif [ "$ST" = "FAILED" ] || [ "$ST" = "CANCELED" ]; then echo "CE $ST"; exit 1; fi
                            sleep 5
                        done
                        [ -n "$ANALYSIS_ID" ] || { echo "CE timeout"; exit 1; }
                        GATE=$(curl -s -u "$TOKEN:" "$SONAR_HOST_URL/api/qualitygates/project_status?analysisId=$ANALYSIS_ID")
                        GST=$(echo "$GATE" | grep -o '"status":"[A-Z]*"' | head -1 | cut -d'"' -f4)
                        echo "Quality gate: ${GST:-UNKNOWN}"
                        if [ "$GST" != "OK" ]; then echo "$GATE"; exit 1; fi
                    '''
                }
            }
        }

        stage("Extract Artifacts") {
            steps {
                // Container and scratch dir both carry the branch; the fixed "<app>-out" and
                // /tmp/<app>-firmware were shared by concurrent builds of different branches.
                sh '''
                    OUT_CTR="${APP_NAME}-out-$(printf '%s' "${BRANCH_NAME}-${BUILD_NUMBER}" | tr -c 'A-Za-z0-9_.-' '-')"
                    FW_DIR="/tmp/${OUT_CTR}-firmware"
                    docker rm -f "$OUT_CTR" 2>/dev/null || true
                    docker create --name "$OUT_CTR" "stm32-app-build:${VERSION}" 2>/dev/null || true
                    rm -rf "$FW_DIR" && docker cp "$OUT_CTR":/artifacts/ "$FW_DIR/" 2>/dev/null || echo No artifacts dir
                    docker rm "$OUT_CTR" 2>/dev/null || true
                    ls -la "$FW_DIR/" 2>/dev/null || echo No firmware output
                    rm -rf "$FW_DIR"
                '''
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
                // Blocking architecture gate at --threshold 90. The old `-v $(pwd):/project`
                // bind mount is empty under DinD (the Jenkins workspace is a named volume the
                // host daemon sees at a different path), so arch-qube scanned nothing. Instead
                // create the container with anonymous volumes and stream the source in via
                // `tar | docker cp`, then copy the report out. `--ci` exits non-zero if < 90.
                sh '''
                    # Branch in the name: BUILD_NUMBER restarts at 1 on every branch, so two branches building
                    # at once used the same name and one's `docker rm -f` deleted the other's container
                    # (arcana-ios PR-14/PR-15, 2026-09-30: "destination ...:/src must be a directory").
                    AQ="arcana-arch-qube-stm32-$(printf '%s' "${BRANCH_NAME}-${BUILD_NUMBER}" | tr -c 'A-Za-z0-9_.-' '-')"
                    docker rm -f "$AQ" 2>/dev/null || true
                    docker create --name "$AQ" --network devops_default \
                        -v /src -v /output \
                        arcana.boo/arcana/arch-qube:latest \
                        scan /src --framework stm32 --no-ai --ci \
                        --format json,markdown -o /output --threshold 90 || exit 1
                    tar --exclude=./.git --exclude=./arch-qube-reports -C . -cf - . \
                        | docker cp - "$AQ":/src || exit 1
                    docker start -a "$AQ"
                    AQ_RC=$?
                    mkdir -p arch-qube-reports
                    docker cp "$AQ":/output/. arch-qube-reports/ 2>/dev/null || true
                    docker rm -f "$AQ" 2>/dev/null || true
                    exit $AQ_RC
                '''
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

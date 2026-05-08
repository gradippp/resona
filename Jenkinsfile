pipeline {
    agent none

    environment {
        IMAGE_NAME = 'resona'
    }

    stages {
        stage('Prepare') {
            agent any
            steps {
                script {
                    checkout scm
                    sh 'git submodule update --init --recursive'
                    
                    env.GIT_COMMIT_SHORT = sh(script: 'git rev-parse --short HEAD', returnStdout: true).trim()
                    
                    // Extract project version from CMakeLists.txt (specifically from the project() block)
                    env.PROJECT_VERSION = sh(script: "grep -A 5 'project(' CMakeLists.txt | grep 'VERSION' | awk '{print \$2}' | tr -d ' \\r\\n'", returnStdout: true).trim()

                    if (!env.PROJECT_VERSION) {
                        env.PROJECT_VERSION = 'latest'
                    }

                    // Handle missing DOCKER_REGISTRY
                    if (!env.DOCKER_REGISTRY) {
                        echo "WARNING: DOCKER_REGISTRY is not set. Using local namespace."
                        env.REGISTRY_PREFIX = ""
                    } else {
                        env.REGISTRY_PREFIX = "${env.DOCKER_REGISTRY}/"
                    }

                    // Define base multi-arch manifests
                    env.IMAGE_TAG_COMMIT = "${env.REGISTRY_PREFIX}${env.IMAGE_NAME}:${env.GIT_COMMIT_SHORT}"
                    env.IMAGE_TAG_VERSION = "${env.REGISTRY_PREFIX}${env.IMAGE_NAME}:${env.PROJECT_VERSION}"
                    env.IMAGE_TAG_BRANCH = "${env.REGISTRY_PREFIX}${env.IMAGE_NAME}:${env.BRANCH_NAME}"

                    // Multi-arch specific tags for parallel builds
                    env.IMAGE_AMD64_COMMIT = "${env.IMAGE_TAG_COMMIT}-amd64"
                    env.IMAGE_ARM64_COMMIT = "${env.IMAGE_TAG_COMMIT}-arm64"
                    
                    env.IMAGE_AMD64_VERSION = "${env.IMAGE_TAG_VERSION}-amd64"
                    env.IMAGE_ARM64_VERSION = "${env.IMAGE_TAG_VERSION}-arm64"
                    
                    env.IMAGE_AMD64_BRANCH = "${env.IMAGE_TAG_BRANCH}-amd64"
                    env.IMAGE_ARM64_BRANCH = "${env.IMAGE_TAG_BRANCH}-arm64"
                }
            }
        }

        stage('Build Images') {
            when {
                anyOf {
                    branch 'master'
                    branch 'dev'
                }
            }
            parallel {
                stage('Build ARM64') {
                    agent { label 'arm64' }
                    steps {
                        script {
                            checkout scm
                            sh 'git submodule update --init --recursive'
                            docker.withRegistry("https://${env.DOCKER_REGISTRY}", env.DOCKER_CREDS_ID) {
                                sh '''
                                docker build \
                                    -t $IMAGE_ARM64_COMMIT \
                                    -t $IMAGE_ARM64_VERSION \
                                    -t $IMAGE_ARM64_BRANCH .
                                docker push $IMAGE_ARM64_COMMIT
                                docker push $IMAGE_ARM64_VERSION
                                docker push $IMAGE_ARM64_BRANCH
                                '''
                            }
                        }
                    }
                }

                stage('Build AMD64') {
                    agent { label 'amd64' }
                    steps {
                        script {
                            checkout scm
                            sh 'git submodule update --init --recursive'
                            docker.withRegistry("https://${env.DOCKER_REGISTRY}", env.DOCKER_CREDS_ID) {
                                sh '''
                                docker build \
                                    -t $IMAGE_AMD64_COMMIT \
                                    -t $IMAGE_AMD64_VERSION \
                                    -t $IMAGE_AMD64_BRANCH .
                                docker push $IMAGE_AMD64_COMMIT
                                docker push $IMAGE_AMD64_VERSION
                                docker push $IMAGE_AMD64_BRANCH
                                '''
                            }
                        }
                    }
                }
            }
        }

        stage('Create Manifests') {
            when {
                anyOf {
                    branch 'master'
                    branch 'dev'
                }
            }
            agent { label 'arm64' }

            steps {
                script {
                    docker.withRegistry("https://${env.DOCKER_REGISTRY}", env.DOCKER_CREDS_ID) {
                        sh '''
                        # Manifest for Commit Hash
                        docker manifest create $IMAGE_TAG_COMMIT \
                            --amend $IMAGE_ARM64_COMMIT \
                            --amend $IMAGE_AMD64_COMMIT
                        docker manifest push $IMAGE_TAG_COMMIT

                        # Manifest for Version
                        docker manifest create $IMAGE_TAG_VERSION \
                            --amend $IMAGE_ARM64_VERSION \
                            --amend $IMAGE_AMD64_VERSION
                        docker manifest push $IMAGE_TAG_VERSION

                        # Manifest for Branch (master/dev)
                        docker manifest create $IMAGE_TAG_BRANCH \
                            --amend $IMAGE_ARM64_BRANCH \
                            --amend $IMAGE_AMD64_BRANCH
                        docker manifest push $IMAGE_TAG_BRANCH
                        '''

                        if (env.BRANCH_NAME == 'master') {
                            env.IMAGE_TAG_LATEST = "${env.DOCKER_REGISTRY}/${env.IMAGE_NAME}:latest"
                            sh '''
                            docker manifest create $IMAGE_TAG_LATEST \
                                --amend $IMAGE_ARM64_COMMIT \
                                --amend $IMAGE_AMD64_COMMIT
                            docker manifest push $IMAGE_TAG_LATEST
                            '''
                        }
                    }
                }
            }
        }

        stage('Cleanup') {
            agent { label 'arm64' }
            steps {
                cleanWs()
            }
        }
    }
}

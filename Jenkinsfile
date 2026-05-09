pipeline {
    agent none

    environment {
        IMAGE_NAME = 'resona'
        // Look up the registry URL from secret text
        DOCKER_REGISTRY_URL = credentials('registry_url')
        DOCKER_CREDS_ID = 'registry_cred' 
    }

    stages {
        stage('Prepare') {
            agent any
            steps {
                script {
                    checkout([$class: 'GitSCM', 
                        branches: scm.branches, 
                        doGenerateSubmoduleConfigurations: scm.doGenerateSubmoduleConfigurations, 
                        extensions: scm.extensions + [[$class: 'CloneOption', depth: 1, noTags: false, reference: '', shallow: true]], 
                        userRemoteConfigs: scm.userRemoteConfigs
                    ])
                    sh 'git submodule update --init --recursive --depth 1'
                    
                    env.GIT_COMMIT_SHORT = sh(script: 'git rev-parse --short HEAD', returnStdout: true).trim()
                    
                    // Robust version extraction using grep -oP
                    env.PROJECT_VERSION = sh(script: "grep -oP 'VERSION\\s+\\K[0-9.]+' CMakeLists.txt | head -1", returnStdout: true).trim()
                    if (!env.PROJECT_VERSION) {
                        env.PROJECT_VERSION = 'latest'
                    }

                    // Define tags (Full path with registry)
                    env.BASE_IMAGE = "${env.DOCKER_REGISTRY_URL}/${env.IMAGE_NAME}"
                    
                    env.IMAGE_TAG_COMMIT = "${env.BASE_IMAGE}:${env.GIT_COMMIT_SHORT}"
                    env.IMAGE_TAG_VERSION = "${env.BASE_IMAGE}:${env.PROJECT_VERSION}"
                    env.IMAGE_TAG_BRANCH = "${env.BASE_IMAGE}:${env.BRANCH_NAME}"
                }
            }
        }

        stage('Build & Push') {
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
                            checkout([$class: 'GitSCM', 
                                branches: scm.branches, 
                                extensions: scm.extensions + [[$class: 'CloneOption', depth: 1, noTags: false, reference: '', shallow: true]], 
                                userRemoteConfigs: scm.userRemoteConfigs
                            ])
                            sh 'git submodule update --init --recursive --depth 1'
                            
                            docker.withRegistry("https://${env.DOCKER_REGISTRY_URL}", env.DOCKER_CREDS_ID) {
                                def customImage = docker.build("${env.IMAGE_TAG_COMMIT}-arm64")
                                customImage.push()
                                customImage.push("${env.PROJECT_VERSION}-arm64")
                                customImage.push("${env.BRANCH_NAME}-arm64")
                            }
                        }
                    }
                }

                stage('Build AMD64') {
                    agent { label 'amd64' }
                    steps {
                        script {
                            checkout([$class: 'GitSCM', 
                                branches: scm.branches, 
                                extensions: scm.extensions + [[$class: 'CloneOption', depth: 1, noTags: false, reference: '', shallow: true]], 
                                userRemoteConfigs: scm.userRemoteConfigs
                            ])
                            sh 'git submodule update --init --recursive --depth 1'
                            
                            docker.withRegistry("https://${env.DOCKER_REGISTRY_URL}", env.DOCKER_CREDS_ID) {
                                def customImage = docker.build("${env.IMAGE_TAG_COMMIT}-amd64")
                                customImage.push()
                                customImage.push("${env.PROJECT_VERSION}-amd64")
                                customImage.push("${env.BRANCH_NAME}-amd64")
                            }
                        }
                    }
                }
            }
        }

        stage('Create Multi-Arch Manifests') {
            when {
                anyOf {
                    branch 'master'
                    branch 'dev'
                }
            }
            agent { label 'arm64' }
            steps {
                script {
                    docker.withRegistry("https://${env.DOCKER_REGISTRY_URL}", env.DOCKER_CREDS_ID) {
                        sh '''
                        # Manifest for Commit
                        docker manifest create $IMAGE_TAG_COMMIT \
                            --amend ${IMAGE_TAG_COMMIT}-arm64 \
                            --amend ${IMAGE_TAG_COMMIT}-amd64
                        docker manifest push $IMAGE_TAG_COMMIT

                        # Manifest for Version
                        docker manifest create $IMAGE_TAG_VERSION \
                            --amend ${IMAGE_TAG_VERSION}-arm64 \
                            --amend ${IMAGE_TAG_VERSION}-amd64
                        docker manifest push $IMAGE_TAG_VERSION

                        # Manifest for Branch
                        docker manifest create $IMAGE_TAG_BRANCH \
                            --amend ${IMAGE_TAG_BRANCH}-arm64 \
                            --amend ${IMAGE_TAG_BRANCH}-amd64
                        docker manifest push $IMAGE_TAG_BRANCH
                        '''

                        if (env.BRANCH_NAME == 'master') {
                            env.IMAGE_TAG_LATEST = "${env.BASE_IMAGE}:latest"
                            sh '''
                            docker manifest create $IMAGE_TAG_LATEST \
                                --amend ${IMAGE_TAG_COMMIT}-arm64 \
                                --amend ${IMAGE_TAG_COMMIT}-amd64
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

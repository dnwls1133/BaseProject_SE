# 다크 판타지 모듈형 로우폴리 리소스

2026-10-21 / Blender 5.0 제작. 모든 제작 파일과 결과물은 이 `Resource` 폴더 안에 있다.
게임 소스, 프로젝트 설정, 런타임 로더는 수정하지 않았다.

## 제공 내용

| 분류 | 내용 |
|---|---|
| 인간형 | 7등신, 기준 신장 1.82m, 교체 부품 37개, 조합 6종(기준 신체 포함) |
| 몬스터 | 동물형 사족·거미형 8족·거대 이족, 각 약함/위험/보스 = 9종, 교체 부품 17개 |
| 건축 | 벽·실제 개구부가 있는 창/문틀·바닥·지붕·박공·기둥·계단·울타리 등 20개 |
| 생활/의례 | 가구·침대·상자·통·선반·그릇·약병·등·도구·제단 등 23개 |
| 자연 | 나무·그루터기·풀·바위·땅·군체·알·균사 뿌리 등 21개 |
| 재질 | 22종 × 512×512 BaseColor/Roughness/Normal/Height = PNG 88장 |
| 애니메이션 | 인간형과 몬스터의 각 리그에 12클립, 별도로 환경의 개폐·바람·맥동 |

실제 GLB는 조립된 캐릭터 기준 **1,672~3,138 삼각형**이다. 사각 면을 삼각형으로 환산해 계산했다.
부품을 무제한으로 중복 부착하면 예산을 넘길 수 있으므로 새 조합은 부품별 수를 합산한다.

## 먼저 열 파일

- `Characters/Human_Modular_Library.blend`: 공통 리그와 모든 인간형 교체 부품. 기본은 순례자 표시.
- `Characters/Character_Lineup.blend`: 순례자·치유사·경비병·수거꾼·감염 방랑자의 정적 비교.
- `Creatures/Animal/Animal_Modular_Library.blend`: 감염 사족형 공통 리그와 부품.
- `Creatures/Spider/Spider_Modular_Library.blend`: 거미형 공통 리그와 부품.
- `Creatures/Giant/Giant_Modular_Library.blend`: 거대 이족형 공통 리그와 부품.
- `Environment/Environment_Modular_Library.blend`: 건축·집기·자연 부품을 펼쳐 놓은 원본.
- `Environment/Settlement_Assembly_Example.blend`: 부품으로 조립한 집과 오염된 주변 환경.

`*_Lineup.blend`는 정적 비교용이다. 애니메이션 편집은 `*_Modular_Library.blend`에서 한다.
교환용 GLB는 `Modules`, `Presets`, 각 환경 분류 폴더에 있으며, 캐릭터 조합 GLB에는 리그와 12개 클립이 함께 있다.

## 조합과 애니메이션

- [조립 규격](Documentation/ASSEMBLY_GUIDE.ko.md)
- [클립·보간·전환 설명](Documentation/ANIMATION_GUIDE.ko.md)
- [애니메이션 조합 데이터](Documentation/animation_blending.json)
- [전체 리소스/삼각형 수 목록](Documentation/asset_manifest.json)
- [파일 내부 확인 결과](Documentation/asset_integrity.json)

Blender 원본은 Z 위, -Y 앞, 미터 단위다. GLB는 Y 위, +Z 앞 좌표로 내보냈다.
메시별 UV와 스킨 가중치를 포함한다. 텍스처는 GLB/원본에 포함하며 독립 PNG도 보관한다.
금속성은 재질별 상수이고, Height는 선택적 후속 작업용이며 현재 GLB에 변위로 적용하지 않는다.

## 확인 범위

Blender에서 메시·리그·키프레임·원본/GLB 저장을 수행했다. 캐릭터/몬스터/조립 환경과 동작 중간 자세를 렌더해 확인했고,
내보낸 GLB의 삼각형 수, 스켈레톤, 스킨 가중치 합, 애니메이션 채널과 길이를 파일 내부에서 조사했다.
이 확인은 게임 빌드·실행·충돌·실제 지형에서의 발 접지·전환 품질 검증을 뜻하지 않는다.

직접 확인할 항목은 부품 교체 후 관절·옷 겹침, 12클립 재생, 이동 속도와 보행 위상,
회전 루트의 중복 적용 여부, 점프의 실제 이동 경로, 건축 그리드와 콜리전 연결이다.
얼굴 표정용 블렌드셰이프, 손가락별 표정 애니메이션, 완전한 천 시뮬레이션은 포함하지 않는다.

`Authoring`의 Python 파일은 Blender 리소스 제작 도구다. 게임 실행에 필요한 코드가 아니다.
프로그램 실행 중에는 인터넷·유료 생성 서비스·외부 모델을 사용하지 않았다.

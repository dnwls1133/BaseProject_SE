# GameWorldConcept section 18. Narrative definition v1; runtime checkpoint schema is separate.
version 1
# profile npc past motive boundary belief unknown + ten action score offsets
profile church_0 "성혈 배급이 끊긴 수도원에서 환자들이 죽는 것을 보았다." "투여를 끊으면 죽는 주민의 명단과 공급망을 지킨다." "무고한 환자의 공개 처형을 거부한다. 공급 단절 때는 격리 압박을 받는다." "치료 장부와 이레나의 진료를 신뢰한다." "모든 변이의 원인과 초기 장부 편집의 이유는 모른다." 0 0.3 0.6 0.5 0.2 0 -0.5 0 -0.3 0
profile church_3 "성혈로 살린 환자가 훗날 변이하는 과정을 돌보았다." "환자가 치료의 대가를 알고 선택할 시간을 지킨다." "동의 없는 실험을 거부하며 공격 중에는 환자 대피를 우선한다." "환자의 진료 기록과 레아의 의료 관찰을 신뢰한다." "안전한 대체 치료와 자신의 성혈 의존 여부는 미정이다." 0.2 0 0 1.2 -0.2 0.3 -1 0.5 -1 0
profile union_0 "제국 배급이 오지 않던 겨울 주민들과 창고를 열었다." "피난지가 인질이 되지 않을 독립 배급망을 지킨다." "어린이 몫을 담보로 내놓지 않는다. 창고 상실은 징발 압박이 된다." "운송 장부와 로안의 보고를 믿지만 밀라의 가격 전망은 확인한다." "검문 지연은 알지만 검문이 막은 습격은 알지 못한다." 0.2 0.3 0.3 0.8 0.2 0 -0.4 0.1 -0.2 0
profile union_2 "무상 배급만으로 수레와 호위를 유지할 수 없음을 겪었다." "세력 독점 없이 다음 운송을 지속할 교역로를 지킨다." "불량 식량을 속여 팔지 않는다. 운송 손실은 높은 가격을 요구할 압박이다." "실제 재고와 수레꾼의 목격을 우선한다." "숲길이 안전하다는 소문은 직접 확인하지 못했다." 0 0.5 1.2 -0.3 -0.2 0 -0.5 0.5 -0.7 0
profile spore_0 "연결로 실종자를 찾았으나 겪지 않은 장면을 어린 시절로 기억했다." "연결의 도움과 연결을 거부할 자유를 함께 지킨다." "강제 연결을 거부한다. 고립으로 주민이 죽는 상황이 그 선을 압박한다." "서로 다른 주민의 기억과 경계 목격을 대조한다." "변이체의 반응이 이해인지 습관인지, 기억의 주인이 누구인지는 모른다." 0 0.2 0.3 0.8 0.2 0.6 -0.5 0.2 -0.3 0
profile spore_3 "연결을 끊은 뒤 기억을 잃은 주민을 치료했다." "어느 편 환자든 이름과 동의를 말할 시간을 지킨다." "환자 허락 없이 기억을 증거로 쓰지 않는다. 임박한 전염 위험은 격리 압박이다." "환자의 반복 진술과 진료 기록을 믿는다." "기억 상실이 단절 때문인지 투여 중단 때문인지는 모른다." 0.2 0 0.2 1.3 -0.3 0.3 -1 0.7 -1 0
profile research_0 "같은 시술에 다른 결과가 난 기록을 보고 단일 치료법을 경계했다." "검증되지 않은 발표가 환자를 죽이지 않도록 기록을 대조한다." "동의 없는 반복 실험을 거부한다. 기록 소각 위험은 조기 공개 압박이다." "시대가 다른 장부와 의료진의 기록을 비교한다." "초기 장부의 빈 기간이 생긴 이유와 유성의 목적은 모른다." 0 0.1 0.2 0.3 -0.3 1.5 -1 0.3 -0.5 0
profile research_3 "검증 자료를 얻기 전에 환자가 죽는 현장을 보았다." "관찰을 위해 치료를 늦추지 않을 권한을 지킨다." "관찰보다 환자의 치료를 우선한다. 다수를 살릴 단서는 부담의 압박이다." "진료와 환자 증언을 믿으며 모든 감염자가 같다는 주장은 경계한다." "환자별 의존 정도와 성혈의 전체 기전은 아직 모른다." 0.2 0 0 1.3 -0.2 0.5 -1 0.5 -1 0
profile purge_0 "늦은 격리로 전초가 무너진 일과 과도한 봉쇄로 민간인이 죽은 일을 겪었다." "근거 없는 살해를 막고 방어 가능한 경계를 유지한다." "증상만으로 항복한 주민을 소각하지 않는다. 확실한 확산 증거에는 봉쇄를 검토한다." "경비 보고와 루스의 진료를 중시한다." "군집이 어떻게 작동하는지, 숲의 모든 주민이 위험한지는 모른다." 0 0.1 0.3 0.4 0.8 0.4 -0.6 0.2 -0.6 0
profile purge_3 "격리 명령 때문에 살릴 수 있었던 환자를 잃었다." "환자의 이름과 선택을 기록에 남긴다." "증거 없는 가족 분리를 거부한다. 집단 감염이 임박하면 짧은 격리를 검토한다." "현장 진료와 추적 기록, 이레나와 에바의 증언을 비교한다." "성공한 격리와 오판 피해만으로 모든 감염자의 위험을 단정하지 않는다." 0.2 0 0.1 1.3 -0.2 0.5 -1 0.6 -1 0

# Directed six-axis bonds: trust liking respect fear dependence ideology.
bond church_0 church_3 40 20 75 0 65 35
bond church_3 church_0 40 20 75 0 65 35
bond church_0 union_0 -15 0 35 10 70 -10
bond union_0 church_0 -15 0 35 10 70 -10
bond union_0 union_2 15 15 70 5 70 20
bond union_2 union_0 15 15 70 5 70 20
bond union_0 spore_0 20 5 45 15 35 -15
bond spore_0 union_0 20 5 45 15 35 -15
bond spore_0 spore_3 55 40 65 0 45 40
bond spore_3 spore_0 55 40 65 0 45 40
bond spore_0 research_0 10 5 50 25 65 -15
bond research_0 spore_0 10 5 50 25 65 -15
bond research_0 research_3 45 25 70 0 60 25
bond research_3 research_0 45 25 70 0 60 25
bond purge_0 purge_3 45 20 80 5 75 25
bond purge_3 purge_0 45 20 80 5 75 25
bond purge_0 union_0 0 0 45 5 75 -20
bond union_0 purge_0 0 0 45 5 75 -20
bond purge_0 spore_0 -45 -20 15 55 10 -60
bond spore_0 purge_0 -45 -20 15 55 10 -60
bond church_3 research_3 65 40 70 0 40 5
bond research_3 church_3 65 40 70 0 40 5
bond research_3 spore_3 50 25 65 15 40 0
bond spore_3 research_3 50 25 65 15 40 0
bond spore_3 purge_3 65 20 80 20 40 -35
bond purge_3 spore_3 65 20 80 20 40 -35
bond church_3 purge_3 45 25 65 15 35 -20
bond purge_3 church_3 45 25 65 15 35 -20
bond union_2 church_2 10 0 50 10 70 0
bond church_2 union_2 10 0 50 10 70 0
bond research_0 church_4 20 10 70 5 70 -10
bond church_4 research_0 20 10 70 5 70 -10
bond union_2 church_0 5 0 45 5 65 -15
bond church_0 union_2 5 0 45 5 65 -15

# fact id assertion correction region observation threshold
fact ration_shortage "북부 창고의 배급 재고가 기준 아래로 떨어졌다." "북부 창고의 재고가 다시 기준 이상으로 회복되었다." refuge food .55
fact transport_accounts "밀라는 수레와 호위를 유지하려면 거래 대가가 필요하다고 보고했다." "운송 손익에 대한 이전 보고가 정정되었다." refuge testimony 0
fact border_injury "성혈 습지 경계에서 부상자를 보았다. 먼저 공격한 주체는 확인하지 못했다." "해당 부상 보고는 더 이상 현재 상태를 설명하지 않는다." marsh injury 90
fact brood_rivalry "서로 다른 변이체 군집이 싸우는 것을 목격했다. 모든 군집이 한편인 것은 아니다." "군집 관계에 관한 이전 해석을 재검토해야 한다." marsh rivals 0
fact incomplete_ledger "침수된 원문은 치료 성공과 기억 변화를 함께 기록하지만 중간 페이지가 비어 있다." "원문에 대한 이전 해석이 정정되었다." shallows site 0
fact withdrawal_risk "의료진은 투여 중단 뒤 쇠약해진 환자를 진료했다. 모든 감염자가 같지는 않다." "투여 중단 위험에 대한 보고를 재검토해야 한다." pilgrimage testimony 0
fact consent_record "환자 동의와 반복 진술을 기록해야 한다는 의료진의 의견이 있다." "동의 기록의 해석이 수정되었다." pilgrimage testimony 0
fact quarantine_harm "격리는 확산을 막기도 했지만 오판으로 민간인이 죽기도 했다." "격리 결과에 대한 기존 보고가 수정되었다." graves testimony 0
knowledge union_2 transport_accounts union_2 1 .95
knowledge union_0 transport_accounts union_2 1 .95
knowledge church_0 withdrawal_risk church_3 1 .95
knowledge church_3 withdrawal_risk church_3 1 .95
knowledge research_3 withdrawal_risk research_3 1 .95
knowledge spore_3 consent_record spore_3 1 .95
knowledge spore_0 consent_record spore_3 1 .95
knowledge church_3 consent_record church_3 1 .95
knowledge purge_3 quarantine_harm purge_3 1 .95
knowledge purge_0 quarantine_harm purge_3 1 .95

# incident id title kind region actor partner trigger evidence lifetime-world-minutes
incident event_refuge_ration "북부 피난지의 빈 배급표" ration refuge union_2 church_0 ration_shortage transport_accounts 720
incident event_forest_signal "숲 경계의 잘못 읽힌 신호" signal marsh spore_3 purge_3 border_injury brood_rivalry 600
incident event_flooded_ledger "침수 기록의 공개 시점" ledger shallows research_0 research_3 incomplete_ledger withdrawal_risk 720

# influence evidence war-restraint + ten action score offsets
influence ration_shortage 0 0 .5 .8 .5 0 0 0 0 0 0
influence transport_accounts 0 0 .2 .5 0 0 0 -.3 .2 -.2 0
influence brood_rivalry .65 0 0 0 .2 .2 .8 -.4 .1 -.6 0
influence withdrawal_risk 0 0 0 .1 .8 0 .6 -.2 0 -.2 0
influence quarantine_harm .25 0 0 .2 .5 .3 .5 -.4 .2 -.3 0
influence incomplete_ledger 0 0 .2 0 .2 0 .8 0 0 0 0
influence consent_record 0 0 0 .2 .5 0 .2 -.3 0 -.2 0

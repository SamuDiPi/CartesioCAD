// Generato da scripts/canalo/gentests.js con i calcoli della pagina CanaloCAD: non modificare a mano.
// clang-format off
void canaloCase0()
{
    canalo::Tray t;
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_4G10")); c.code += "-1"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_4G10")); c.code += "-2"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_5G6")); c.code += "-3"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_3G2.5")); c.code += "-4"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    const canalo::Metrics m = canalo::recalculate(t);
    CHECK(approx(m.fillPct, 6.5, 1e-9));
    CHECK(approx(m.sparePct, 93.5, 1e-9));
    CHECK(approx(m.kr, 0.68, 1e-9));
    CHECK(approx(m.nLayers, 1.0, 1e-9));
    CHECK(approx(m.totalKgM, 8.25, 1e-9));
    CHECK(approx(m.qMaxKgM, 999.0, 1e-9));
    CHECK(approx(m.pointLoad, 12.4, 1e-9));
    CHECK(approx(m.maxPoint, 0.0, 1e-9));
    CHECK(approx(m.loadPct, 0.0, 1e-9));
    CHECK(approx(m.deflectionMm, 0.0, 1e-9));
    CHECK(approx(m.maxDeflectionMm, 7.5, 1e-9));
    CHECK(approx(m.pullDaN, 25.0, 1e-9));
    CHECK(approx(m.maxPullDaN, 415.0, 1e-9));
    CHECK(approx(m.maxBendR, 195.0, 1e-9));
    CHECK(approx(m.fitR, 300.0, 1e-9));
    CHECK(approx(m.jouleWm, 0.0, 1e-9));
    CHECK(approx(m.maxDv, 0.0, 1e-9));
    CHECK(m.segConform == true);
    CHECK(m.segDetail == "Solo Cavi BT Potenza (Kr=0.68 su 1 str.)");
    CHECK(m.critCode == "FG16OM16-4G10-1");
    CHECK(m.deratingFailCode == "");
    CHECK(approx(t.cables[0].x, 10.3, 1e-9) && approx(t.cables[0].y, 9.8, 1e-9));
    CHECK(approx(t.cables[1].x, 30.7, 1e-9) && approx(t.cables[1].y, 9.8, 1e-9));
    CHECK(approx(t.cables[2].x, 50.4, 1e-9) && approx(t.cables[2].y, 9.1, 1e-9));
    CHECK(approx(t.cables[3].x, 66.0, 1e-9) && approx(t.cables[3].y, 6.1, 1e-9));
    CHECK(m.bom.size() == 4);
    CHECK(m.bom.size() > 0 && m.bom[0].code == "PAS-200x75" && m.bom[0].desc == "Passerella asolata in lamiera forata L=200xH=75mm sp.1.5mm" && approx(m.bom[0].qty, 30.0) && m.bom[0].note == "10 verghe da 3m");
    CHECK(m.bom.size() > 1 && m.bom[1].code == "PAS-COP-200" && m.bom[1].desc == "Coperchio superiore (snap) L=200mm" && approx(m.bom[1].qty, 30.0) && m.bom[1].note == "Inclusi fermacoperchi");
    CHECK(m.bom.size() > 2 && m.bom[2].code == "PAS-GIU-M6" && m.bom[2].desc == "Kit giunzione lineare rinforzata con bulloneria M6" && approx(m.bom[2].qty, 10.0) && m.bom[2].note == "1 ogni 3m");
    CHECK(m.bom.size() > 3 && m.bom[3].code == "PAS-PE-16" && m.bom[3].desc == "Morsetto equipotenziale + capocorda M6 + conduttore PE (GV16)" && approx(m.bom[3].qty, 4.0) && m.bom[3].note == "1 ogni 12m");
}

void canaloCase1()
{
    canalo::Tray t;
    t.support = "wall_med";
    t.spanM = 1.5;
    t.bends90 = 2;
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_1X95")); c.code += "-1"; c.trefoil = true; c.trefoilGroup = "T1"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_1X95")); c.code += "-2"; c.trefoil = true; c.trefoilGroup = "T1"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_1X95")); c.code += "-3"; c.trefoil = true; c.trefoilGroup = "T1"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_5G6")); c.code += "-4"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_5G6")); c.code += "-5"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("DATA_CAT6")); c.code += "-6"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("DATA_CAT6")); c.code += "-7"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("DATA_CAT6")); c.code += "-8"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("DATA_CAT6")); c.code += "-9"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    const canalo::Metrics m = canalo::recalculate(t);
    CHECK(approx(m.fillPct, 10.3, 1e-9));
    CHECK(approx(m.sparePct, 89.7, 1e-9));
    CHECK(approx(m.kr, 0.47, 1e-9));
    CHECK(approx(m.nLayers, 3.0, 1e-9));
    CHECK(approx(m.totalKgM, 10.47, 1e-9));
    CHECK(approx(m.qMaxKgM, 50.0, 1e-9));
    CHECK(approx(m.pointLoad, 15.7, 1e-9));
    CHECK(approx(m.maxPoint, 75.0, 1e-9));
    CHECK(approx(m.loadPct, 20.9, 1e-9));
    CHECK(approx(m.deflectionMm, 1.57, 1e-9));
    CHECK(approx(m.maxDeflectionMm, 7.5, 1e-9));
    CHECK(approx(m.pullDaN, 145.0, 1e-9));
    CHECK(approx(m.maxPullDaN, 1625.0, 1e-9));
    CHECK(approx(m.maxBendR, 238.0, 1e-9));
    CHECK(approx(m.fitR, 300.0, 1e-9));
    CHECK(approx(m.jouleWm, 0.0, 1e-9));
    CHECK(approx(m.maxDv, 0.0, 1e-9));
    CHECK(m.segConform == false);
    CHECK(m.segDetail == "Conflitto UTP-CAT6-4X2-6: inserire Setto!");
    CHECK(m.critCode == "FG16M16-1X95-1");
    CHECK(m.deratingFailCode == "");
    CHECK(approx(t.cables[0].x, 10.5, 1e-9) && approx(t.cables[0].y, 9.9, 1e-9));
    CHECK(approx(t.cables[1].x, 30.3, 1e-9) && approx(t.cables[1].y, 9.9, 1e-9));
    CHECK(approx(t.cables[2].x, 20.4, 1e-9) && approx(t.cables[2].y, 27.0, 1e-9));
    CHECK(approx(t.cables[3].x, 49.8, 1e-9) && approx(t.cables[3].y, 9.1, 1e-9));
    CHECK(approx(t.cables[4].x, 68.4, 1e-9) && approx(t.cables[4].y, 9.1, 1e-9));
    CHECK(approx(t.cables[5].x, 81.4, 1e-9) && approx(t.cables[5].y, 2.9, 1e-9));
    CHECK(approx(t.cables[6].x, 88.0, 1e-9) && approx(t.cables[6].y, 2.9, 1e-9));
    CHECK(approx(t.cables[7].x, 94.6, 1e-9) && approx(t.cables[7].y, 2.9, 1e-9));
    CHECK(approx(t.cables[8].x, 101.2, 1e-9) && approx(t.cables[8].y, 2.9, 1e-9));
    CHECK(m.bom.size() == 6);
    CHECK(m.bom.size() > 0 && m.bom[0].code == "PAS-200x75" && m.bom[0].desc == "Passerella asolata in lamiera forata L=200xH=75mm sp.1.5mm" && approx(m.bom[0].qty, 30.0) && m.bom[0].note == "10 verghe da 3m");
    CHECK(m.bom.size() > 1 && m.bom[1].code == "PAS-COP-200" && m.bom[1].desc == "Coperchio superiore (snap) L=200mm" && approx(m.bom[1].qty, 30.0) && m.bom[1].note == "Inclusi fermacoperchi");
    CHECK(m.bom.size() > 2 && m.bom[2].code == "PAS-GIU-M6" && m.bom[2].desc == "Kit giunzione lineare rinforzata con bulloneria M6" && approx(m.bom[2].qty, 10.0) && m.bom[2].note == "1 ogni 3m");
    CHECK(m.bom.size() > 3 && m.bom[3].code == "PAS-STF-200" && m.bom[3].desc == "Mensola Rinforzata con Saetta L=250mm (Passo Ls=1.5m)" && approx(m.bom[3].qty, 21.0) && m.bom[3].note == "Inclusi 42 tasselli M10");
    CHECK(m.bom.size() > 4 && m.bom[4].code == "PAS-CRV90-200-R300" && m.bom[4].desc == "Curva piana 90° L=200mm H=75mm (R_acc=300mm >= R_min=238mm)" && approx(m.bom[4].qty, 2.0) && m.bom[4].note == "Cavo critico: FG16M16-1X95-1");
    CHECK(m.bom.size() > 5 && m.bom[5].code == "PAS-PE-16" && m.bom[5].desc == "Morsetto equipotenziale + capocorda M6 + conduttore PE (GV16)" && approx(m.bom[5].qty, 4.0) && m.bom[5].note == "1 ogni 12m");
}

void canaloCase2()
{
    canalo::Tray t;
    t.septums = 1;
    t.support = "ceiling_trap";
    t.spanM = 2.0;
    t.tees = 1;
    t.reducers = 2;
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_4G16")); c.code += "-1"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_4G16")); c.code += "-2"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_3G4")); c.code += "-3"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("LIYCY_2X0.75")); c.code += "-4"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("BUS_KNX")); c.code += "-5"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("DATA_CAT6A")); c.code += "-6"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    const canalo::Metrics m = canalo::recalculate(t);
    CHECK(approx(m.fillPct, 7.1, 1e-9));
    CHECK(approx(m.sparePct, 92.9, 1e-9));
    CHECK(approx(m.kr, 0.54, 1e-9));
    CHECK(approx(m.nLayers, 2.0, 1e-9));
    CHECK(approx(m.totalKgM, 8.65, 1e-9));
    CHECK(approx(m.qMaxKgM, 89.6, 1e-9));
    CHECK(approx(m.pointLoad, 17.3, 1e-9));
    CHECK(approx(m.maxPoint, 179.0, 1e-9));
    CHECK(approx(m.loadPct, 9.7, 1e-9));
    CHECK(approx(m.deflectionMm, 0.97, 1e-9));
    CHECK(approx(m.maxDeflectionMm, 10.0, 1e-9));
    CHECK(approx(m.pullDaN, 29.2, 1e-9));
    CHECK(approx(m.maxPullDaN, 538.0, 1e-9));
    CHECK(approx(m.maxBendR, 228.0, 1e-9));
    CHECK(approx(m.fitR, 300.0, 1e-9));
    CHECK(approx(m.jouleWm, 0.0, 1e-9));
    CHECK(approx(m.maxDv, 0.0, 1e-9));
    CHECK(m.segConform == true);
    CHECK(m.segDetail == "Art. 528.1 OK (Kr=0.54 su 2 str.)");
    CHECK(m.critCode == "FG16OM16-4G16-1");
    CHECK(m.deratingFailCode == "");
    CHECK(approx(t.cables[0].x, 11.9, 1e-9) && approx(t.cables[0].y, 11.4, 1e-9));
    CHECK(approx(t.cables[1].x, 35.3, 1e-9) && approx(t.cables[1].y, 11.4, 1e-9));
    CHECK(approx(t.cables[2].x, 54.2, 1e-9) && approx(t.cables[2].y, 6.9, 1e-9));
    CHECK(approx(t.cables[3].x, 139.1, 1e-9) && approx(t.cables[3].y, 2.9, 1e-9));
    CHECK(approx(t.cables[4].x, 132.4, 1e-9) && approx(t.cables[4].y, 3.4, 1e-9));
    CHECK(approx(t.cables[5].x, 124.9, 1e-9) && approx(t.cables[5].y, 3.6, 1e-9));
    CHECK(m.bom.size() == 8);
    CHECK(m.bom.size() > 0 && m.bom[0].code == "PAS-200x75" && m.bom[0].desc == "Passerella asolata in lamiera forata L=200xH=75mm sp.1.5mm" && approx(m.bom[0].qty, 30.0) && m.bom[0].note == "10 verghe da 3m");
    CHECK(m.bom.size() > 1 && m.bom[1].code == "PAS-COP-200" && m.bom[1].desc == "Coperchio superiore (snap) L=200mm" && approx(m.bom[1].qty, 30.0) && m.bom[1].note == "Inclusi fermacoperchi");
    CHECK(m.bom.size() > 2 && m.bom[2].code == "PAS-SET-H75" && m.bom[2].desc == "Setto separatore longitudinale H=75mm (1 file)" && approx(m.bom[2].qty, 30.0) && m.bom[2].note == "Segregazione CEI 64-8");
    CHECK(m.bom.size() > 3 && m.bom[3].code == "PAS-GIU-M6" && m.bom[3].desc == "Kit giunzione lineare rinforzata con bulloneria M6" && approx(m.bom[3].qty, 10.0) && m.bom[3].note == "1 ogni 3m");
    CHECK(m.bom.size() > 4 && m.bom[4].code == "PAS-STF-200" && m.bom[4].desc == "Trapezio Soffitto 2xM10 + C40 L=250mm (Passo Ls=2m)" && approx(m.bom[4].qty, 16.0) && m.bom[4].note == "Inclusi 32 tasselli M10");
    CHECK(m.bom.size() > 5 && m.bom[5].code == "PAS-DEVT-200" && m.bom[5].desc == "Deviazione piana a T L=200mm H=75mm completa di giunti" && approx(m.bom[5].qty, 1.0) && m.bom[5].note == "Raggio R=300mm");
    CHECK(m.bom.size() > 6 && m.bom[6].code == "PAS-RID-200" && m.bom[6].desc == "Riduzione simmetrica/asimmetrica di larghezza L=200mm" && approx(m.bom[6].qty, 2.0) && m.bom[6].note == "Acciaio zincato");
    CHECK(m.bom.size() > 7 && m.bom[7].code == "PAS-PE-16" && m.bom[7].desc == "Morsetto equipotenziale + capocorda M6 + conduttore PE (GV16)" && approx(m.bom[7].qty, 4.0) && m.bom[7].note == "1 ogni 12m");
}

void canaloCase3()
{
    canalo::Tray t;
    t.width = 300.0;
    t.height = 100.0;
    t.thk = 2.0;
    t.septums = 2;
    t.trayType = "solid";
    t.cover = "none";
    t.support = "wall_heavy";
    t.spanM = 2.5;
    t.seismic = "cable_9m";
    t.reiCount = 2;
    t.earth = "none";
    t.brand = "TIPOLOGIE";
    t.seriesId = "CAN_CIE";
    t.lengthM = 47.0;
    t.pdfLoaded = true;
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_4G25")); c.code += "-1"; c.ib = 90.0; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_4G25")); c.code += "-2"; c.ib = 80.0; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_5G10")); c.code += "-3"; c.ib = 40.0; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FTG18_4G10")); c.code += "-4"; c.ib = 50.0; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FTG18_3G2.5")); c.code += "-5"; c.ib = 10.0; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("IRAI_LOOP")); c.code += "-6"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("EVAC_100V")); c.code += "-7"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FO_8_OM4")); c.code += "-8"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    const canalo::Metrics m = canalo::recalculate(t);
    CHECK(approx(m.fillPct, 7.2, 1e-9));
    CHECK(approx(m.sparePct, 92.8, 1e-9));
    CHECK(approx(m.kr, 0.55, 1e-9));
    CHECK(approx(m.nLayers, 2.0, 1e-9));
    CHECK(approx(m.totalKgM, 13.8, 1e-9));
    CHECK(approx(m.qMaxKgM, 57.9, 1e-9));
    CHECK(approx(m.pointLoad, 34.5, 1e-9));
    CHECK(approx(m.maxPoint, 145.0, 1e-9));
    CHECK(approx(m.loadPct, 23.8, 1e-9));
    CHECK(approx(m.deflectionMm, 2.38, 1e-9));
    CHECK(approx(m.maxDeflectionMm, 10.0, 1e-9));
    CHECK(approx(m.pullDaN, 93.8, 1e-9));
    CHECK(approx(m.maxPullDaN, 1117.0, 1e-9));
    CHECK(approx(m.maxBendR, 272.0, 1e-9));
    CHECK(approx(m.fitR, 300.0, 1e-9));
    CHECK(approx(m.jouleWm, 65.3, 1e-9));
    CHECK(approx(m.maxDv, 3.15, 1e-9));
    CHECK(m.segConform == true);
    CHECK(m.segDetail == "Art. 528.1 OK (Kr=0.55 su 2 str.)");
    CHECK(m.critCode == "FG16OM16-4G25-1");
    CHECK(m.deratingFailCode == "FG16OM16-4G25-1");
    CHECK(approx(t.cables[0].x, 14.1, 1e-9) && approx(t.cables[0].y, 13.6, 1e-9));
    CHECK(approx(t.cables[1].x, 41.7, 1e-9) && approx(t.cables[1].y, 13.6, 1e-9));
    CHECK(approx(t.cables[2].x, 66.4, 1e-9) && approx(t.cables[2].y, 10.7, 1e-9));
    CHECK(approx(t.cables[3].x, 113.6, 1e-9) && approx(t.cables[3].y, 10.1, 1e-9));
    CHECK(approx(t.cables[4].x, 130.9, 1e-9) && approx(t.cables[4].y, 6.4, 1e-9));
    CHECK(approx(t.cables[5].x, 220.4, 1e-9) && approx(t.cables[5].y, 4.1, 1e-9));
    CHECK(approx(t.cables[6].x, 210.4, 1e-9) && approx(t.cables[6].y, 4.9, 1e-9));
    CHECK(approx(t.cables[7].x, 229.2, 1e-9) && approx(t.cables[7].y, 3.9, 1e-9));
    CHECK(m.bom.size() == 6);
    CHECK(m.bom.size() > 0 && m.bom[0].code == "CAN-300x100" && m.bom[0].desc == "Canale portacavi cieco in lamiera piena L=300xH=100mm sp.2mm" && approx(m.bom[0].qty, 48.0) && m.bom[0].note == "16 verghe da 3m");
    CHECK(m.bom.size() > 1 && m.bom[1].code == "CAN-SET-H100" && m.bom[1].desc == "Setto separatore longitudinale H=100mm (2 file)" && approx(m.bom[1].qty, 96.0) && m.bom[1].note == "Segregazione CEI 64-8");
    CHECK(m.bom.size() > 2 && m.bom[2].code == "CAN-GIU-M6" && m.bom[2].desc == "Kit giunzione lineare rinforzata con bulloneria M6" && approx(m.bom[2].qty, 16.0) && m.bom[2].note == "1 ogni 3m");
    CHECK(m.bom.size() > 3 && m.bom[3].code == "CAN-STF-300" && m.bom[3].desc == "Mensola Parete Pesante Doppia C50 L=350mm (Passo Ls=2.5m)" && approx(m.bom[3].qty, 20.0) && m.bom[3].note == "Inclusi 40 tasselli M10");
    CHECK(m.bom.size() > 4 && m.bom[4].code == "CAN-SISM-CABLE_9M" && m.bom[4].desc == "Kit controvento antisismico NTC 2018 (Funi 45° ogni 9m)" && approx(m.bom[4].qty, 6.0) && m.bom[4].note == "Rif. NTC 2018");
    CHECK(m.bom.size() > 5 && m.bom[5].code == "FIRE-EI120-300x100" && m.bom[5].desc == "Barriera tagliafuoco intumescente EI120 (cuscini + mastice endotermico per 279 cm² netti/varco)" && approx(m.bom[5].qty, 2.0) && m.bom[5].note == "16 cuscini intumescenti tot.");
}

void canaloCase4()
{
    canalo::Tray t;
    t.septums = 1;
    t.cover = "clamped";
    t.trayType = "mesh";
    t.support = "floor_omega";
    t.spanM = 3.0;
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_1X50")); c.code += "-1"; c.x = 20.0; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_1X50")); c.code += "-2"; c.x = 40.0; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_4G4")); c.code += "-3"; c.x = 150.0; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_4G4")); c.code += "-4"; c.x = 170.0; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("H07RN_4G4")); c.code += "-5"; c.x = 180.0; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    const canalo::Metrics m = canalo::recalculate(t);
    CHECK(approx(m.fillPct, 6.5, 1e-9));
    CHECK(approx(m.sparePct, 93.5, 1e-9));
    CHECK(approx(m.kr, 0.68, 1e-9));
    CHECK(approx(m.nLayers, 1.0, 1e-9));
    CHECK(approx(m.totalKgM, 9.3, 1e-9));
    CHECK(approx(m.qMaxKgM, 41.0, 1e-9));
    CHECK(approx(m.pointLoad, 27.9, 1e-9));
    CHECK(approx(m.maxPoint, 123.0, 1e-9));
    CHECK(approx(m.loadPct, 22.7, 1e-9));
    CHECK(approx(m.deflectionMm, 2.27, 1e-9));
    CHECK(approx(m.maxDeflectionMm, 10.0, 1e-9));
    CHECK(approx(m.pullDaN, 25.2, 1e-9));
    CHECK(approx(m.maxPullDaN, 680.0, 1e-9));
    CHECK(approx(m.maxBendR, 185.0, 1e-9));
    CHECK(approx(m.fitR, 300.0, 1e-9));
    CHECK(approx(m.jouleWm, 0.0, 1e-9));
    CHECK(approx(m.maxDv, 0.0, 1e-9));
    CHECK(m.segConform == true);
    CHECK(m.segDetail == "Solo Cavi BT Potenza (Kr=0.68 su 1 str.)");
    CHECK(m.critCode == "FG16M16-1X50-1");
    CHECK(m.deratingFailCode == "");
    CHECK(approx(t.cables[0].x, 8.2, 1e-9) && approx(t.cables[0].y, 7.7, 1e-9));
    CHECK(approx(t.cables[1].x, 24.4, 1e-9) && approx(t.cables[1].y, 7.7, 1e-9));
    CHECK(approx(t.cables[2].x, 146.8, 1e-9) && approx(t.cables[2].y, 7.6, 1e-9));
    CHECK(approx(t.cables[3].x, 162.4, 1e-9) && approx(t.cables[3].y, 7.6, 1e-9));
    CHECK(approx(t.cables[4].x, 130.0, 1e-9) && approx(t.cables[4].y, 8.8, 1e-9));
    CHECK(m.bom.size() == 6);
    CHECK(m.bom.size() > 0 && m.bom[0].code == "PFI-200x75" && m.bom[0].desc == "Passerella a filo (rete elettrosaldata) L=200xH=75mm sp.1.5mm" && approx(m.bom[0].qty, 30.0) && m.bom[0].note == "10 verghe da 3m");
    CHECK(m.bom.size() > 1 && m.bom[1].code == "PFI-COP-200" && m.bom[1].desc == "Coperchio superiore (clamped) L=200mm" && approx(m.bom[1].qty, 30.0) && m.bom[1].note == "Inclusi fermacoperchi");
    CHECK(m.bom.size() > 2 && m.bom[2].code == "PFI-SET-H75" && m.bom[2].desc == "Setto separatore longitudinale H=75mm (1 file)" && approx(m.bom[2].qty, 30.0) && m.bom[2].note == "Segregazione CEI 64-8");
    CHECK(m.bom.size() > 3 && m.bom[3].code == "PFI-GIU-M6" && m.bom[3].desc == "Kit giunzione lineare rinforzata con bulloneria M6" && approx(m.bom[3].qty, 10.0) && m.bom[3].note == "1 ogni 3m");
    CHECK(m.bom.size() > 4 && m.bom[4].code == "PFI-STF-200" && m.bom[4].desc == "Supporto a Pavimento su Omega C40 L=250mm (Passo Ls=3m)" && approx(m.bom[4].qty, 11.0) && m.bom[4].note == "Inclusi 22 tasselli M10");
    CHECK(m.bom.size() > 5 && m.bom[5].code == "PFI-PE-16" && m.bom[5].desc == "Morsetto equipotenziale + capocorda M6 + conduttore PE (GV16)" && approx(m.bom[5].qty, 4.0) && m.bom[5].note == "1 ogni 12m");
}

void canaloCase5()
{
    canalo::Tray t;
    t.packing = "input";
    t.width = 100.0;
    t.height = 60.0;
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_5G16")); c.code += "-1"; c.origX = 20.0; c.origY = 13.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_5G16")); c.code += "-2"; c.origX = 25.0; c.origY = 14.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_3G6")); c.code += "-3"; c.origX = 70.0; c.origY = 30.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("OH2_7G1.5")); c.code += "-4"; c.origX = 90.0; c.origY = 8.0; t.cables.push_back(c); }
    const canalo::Metrics m = canalo::recalculate(t);
    CHECK(approx(m.fillPct, 21.9, 1e-9));
    CHECK(approx(m.sparePct, 78.1, 1e-9));
    CHECK(approx(m.kr, 0.54, 1e-9));
    CHECK(approx(m.nLayers, 2.0, 1e-9));
    CHECK(approx(m.totalKgM, 7.02, 1e-9));
    CHECK(approx(m.qMaxKgM, 999.0, 1e-9));
    CHECK(approx(m.pointLoad, 10.5, 1e-9));
    CHECK(approx(m.maxPoint, 0.0, 1e-9));
    CHECK(approx(m.loadPct, 0.0, 1e-9));
    CHECK(approx(m.deflectionMm, 0.0, 1e-9));
    CHECK(approx(m.maxDeflectionMm, 7.5, 1e-9));
    CHECK(approx(m.pullDaN, 36.3, 1e-9));
    CHECK(approx(m.maxPullDaN, 563.0, 1e-9));
    CHECK(approx(m.maxBendR, 248.0, 1e-9));
    CHECK(approx(m.fitR, 300.0, 1e-9));
    CHECK(approx(m.jouleWm, 0.0, 1e-9));
    CHECK(approx(m.maxDv, 0.0, 1e-9));
    CHECK(m.segConform == true);
    CHECK(m.segDetail == "Solo Cavi BT Potenza (Kr=0.54 su 2 str.)");
    CHECK(m.critCode == "FG16OM16-5G16-1");
    CHECK(m.deratingFailCode == "");
    CHECK(approx(t.cables[0].x, 20.0, 1e-9) && approx(t.cables[0].y, 13.0, 1e-9));
    CHECK(approx(t.cables[1].x, 45.0, 1e-9) && approx(t.cables[1].y, 14.0, 1e-9));
    CHECK(approx(t.cables[2].x, 70.0, 1e-9) && approx(t.cables[2].y, 30.0, 1e-9));
    CHECK(approx(t.cables[3].x, 90.0, 1e-9) && approx(t.cables[3].y, 8.0, 1e-9));
    CHECK(m.bom.size() == 4);
    CHECK(m.bom.size() > 0 && m.bom[0].code == "PAS-100x60" && m.bom[0].desc == "Passerella asolata in lamiera forata L=100xH=60mm sp.1.5mm" && approx(m.bom[0].qty, 30.0) && m.bom[0].note == "10 verghe da 3m");
    CHECK(m.bom.size() > 1 && m.bom[1].code == "PAS-COP-100" && m.bom[1].desc == "Coperchio superiore (snap) L=100mm" && approx(m.bom[1].qty, 30.0) && m.bom[1].note == "Inclusi fermacoperchi");
    CHECK(m.bom.size() > 2 && m.bom[2].code == "PAS-GIU-M6" && m.bom[2].desc == "Kit giunzione lineare rinforzata con bulloneria M6" && approx(m.bom[2].qty, 10.0) && m.bom[2].note == "1 ogni 3m");
    CHECK(m.bom.size() > 3 && m.bom[3].code == "PAS-PE-16" && m.bom[3].desc == "Morsetto equipotenziale + capocorda M6 + conduttore PE (GV16)" && approx(m.bom[3].qty, 4.0) && m.bom[3].note == "1 ogni 12m");
}

void canaloCase6()
{
    canalo::Tray t;
    t.width = 100.0;
    t.height = 50.0;
    t.bends90 = 4;
    t.lengthM = 80.0;
    t.support = "ceiling_single";
    t.spanM = 1.0;
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_1X25")); c.code += "-1"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_4G6")); c.code += "-2"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_1X25")); c.code += "-3"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_4G6")); c.code += "-4"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_1X25")); c.code += "-5"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_4G6")); c.code += "-6"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_1X25")); c.code += "-7"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_4G6")); c.code += "-8"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_1X25")); c.code += "-9"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_4G6")); c.code += "-10"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_1X25")); c.code += "-11"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_4G6")); c.code += "-12"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_1X25")); c.code += "-13"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    { auto c = canalo::cableFrom(*canalo::catalogItem("FG16_4G6")); c.code += "-14"; c.origX = 18.0; c.origY = c.d / 2.0; t.cables.push_back(c); }
    const canalo::Metrics m = canalo::recalculate(t);
    CHECK(approx(m.fillPct, 47.4, 1e-9));
    CHECK(approx(m.sparePct, 52.6, 1e-9));
    CHECK(approx(m.kr, 0.45, 1e-9));
    CHECK(approx(m.nLayers, 3.0, 1e-9));
    CHECK(approx(m.totalKgM, 9.31, 1e-9));
    CHECK(approx(m.qMaxKgM, 48.8, 1e-9));
    CHECK(approx(m.pointLoad, 9.3, 1e-9));
    CHECK(approx(m.maxPoint, 49.0, 1e-9));
    CHECK(approx(m.loadPct, 19.1, 1e-9));
    CHECK(approx(m.deflectionMm, 0.95, 1e-9));
    CHECK(approx(m.maxDeflectionMm, 5.0, 1e-9));
    CHECK(approx(m.pullDaN, 1502.2, 1e-9));
    CHECK(approx(m.maxPullDaN, 1505.0, 1e-9));
    CHECK(approx(m.maxBendR, 134.0, 1e-9));
    CHECK(approx(m.fitR, 150.0, 1e-9));
    CHECK(approx(m.jouleWm, 0.0, 1e-9));
    CHECK(approx(m.maxDv, 0.0, 1e-9));
    CHECK(m.segConform == true);
    CHECK(m.segDetail == "Solo Cavi BT Potenza (Kr=0.45 su 3 str.)");
    CHECK(m.critCode == "FG16OM16-4G6-2");
    CHECK(m.deratingFailCode == "");
    CHECK(approx(t.cables[0].x, 34.8, 1e-9) && approx(t.cables[0].y, 20.6, 1e-9));
    CHECK(approx(t.cables[1].x, 8.9, 1e-9) && approx(t.cables[1].y, 8.4, 1e-9));
    CHECK(approx(t.cables[2].x, 52.2, 1e-9) && approx(t.cables[2].y, 20.6, 1e-9));
    CHECK(approx(t.cables[3].x, 26.3, 1e-9) && approx(t.cables[3].y, 8.4, 1e-9));
    CHECK(approx(t.cables[4].x, 69.6, 1e-9) && approx(t.cables[4].y, 20.6, 1e-9));
    CHECK(approx(t.cables[5].x, 43.7, 1e-9) && approx(t.cables[5].y, 8.4, 1e-9));
    CHECK(approx(t.cables[6].x, 79.2, 1e-9) && approx(t.cables[6].y, 29.1, 1e-9));
    CHECK(approx(t.cables[7].x, 61.1, 1e-9) && approx(t.cables[7].y, 8.4, 1e-9));
    CHECK(approx(t.cables[8].x, 43.2, 1e-9) && approx(t.cables[8].y, 30.0, 1e-9));
    CHECK(approx(t.cables[9].x, 78.5, 1e-9) && approx(t.cables[9].y, 8.4, 1e-9));
    CHECK(approx(t.cables[10].x, 60.6, 1e-9) && approx(t.cables[10].y, 30.0, 1e-9));
    CHECK(approx(t.cables[11].x, 91.1, 1e-9) && approx(t.cables[11].y, 20.1, 1e-9));
    CHECK(approx(t.cables[12].x, 29.4, 1e-9) && approx(t.cables[12].y, 32.1, 1e-9));
    CHECK(approx(t.cables[13].x, 17.3, 1e-9) && approx(t.cables[13].y, 23.4, 1e-9));
    CHECK(m.bom.size() == 6);
    CHECK(m.bom.size() > 0 && m.bom[0].code == "PAS-100x50" && m.bom[0].desc == "Passerella asolata in lamiera forata L=100xH=50mm sp.1.5mm" && approx(m.bom[0].qty, 81.0) && m.bom[0].note == "27 verghe da 3m");
    CHECK(m.bom.size() > 1 && m.bom[1].code == "PAS-COP-100" && m.bom[1].desc == "Coperchio superiore (snap) L=100mm" && approx(m.bom[1].qty, 81.0) && m.bom[1].note == "Inclusi fermacoperchi");
    CHECK(m.bom.size() > 2 && m.bom[2].code == "PAS-GIU-M6" && m.bom[2].desc == "Kit giunzione lineare rinforzata con bulloneria M6" && approx(m.bom[2].qty, 27.0) && m.bom[2].note == "1 ogni 3m");
    CHECK(m.bom.size() > 3 && m.bom[3].code == "PAS-STF-100" && m.bom[3].desc == "Pendino Centrale Monotirante M10 L=150mm (Passo Ls=1m)" && approx(m.bom[3].qty, 81.0) && m.bom[3].note == "Inclusi 162 tasselli M10");
    CHECK(m.bom.size() > 4 && m.bom[4].code == "PAS-CRV90-100-R150" && m.bom[4].desc == "Curva piana 90° L=100mm H=50mm (R_acc=150mm >= R_min=134mm)" && approx(m.bom[4].qty, 4.0) && m.bom[4].note == "Cavo critico: FG16OM16-4G6-2");
    CHECK(m.bom.size() > 5 && m.bom[5].code == "PAS-PE-16" && m.bom[5].desc == "Morsetto equipotenziale + capocorda M6 + conduttore PE (GV16)" && approx(m.bom[5].qty, 8.0) && m.bom[5].note == "1 ogni 12m");
}

void testCanaloAgainstPage()
{
    canaloCase0();
    canaloCase1();
    canaloCase2();
    canaloCase3();
    canaloCase4();
    canaloCase5();
    canaloCase6();
}
// clang-format on

// clang-format off
void canaloSection0()
{
    canalo::Drawn dr;
    dr.fileName = "sezione.dxf";
    dr.segments.push_back({{1000.0, 500.0}, {1303.0, 500.0}, "Canalina"});
    dr.segments.push_back({{1000.0, 500.0}, {1000.0, 576.5}, "Canalina"});
    dr.segments.push_back({{1303.0, 500.0}, {1303.0, 576.5}, "Canalina"});
    dr.circles.push_back({{1020.0, 510.0}, 9.9, "Cavi"});
    dr.circles.push_back({{1041.0, 510.0}, 9.9, "Cavi"});
    dr.circles.push_back({{1062.0, 510.0}, 9.9, "Cavi"});
    dr.circles.push_back({{1083.0, 510.0}, 9.9, "Cavi"});
    dr.circles.push_back({{1120.0, 508.0}, 7.25, "Cavi"});
    dr.circles.push_back({{1120.0, 508.0}, 5.0, "Cavi"});
    dr.circles.push_back({{1160.0, 507.0}, 6.1, "Cavi"});
    dr.circles.push_back({{1200.0, 504.0}, 3.4, "Cavi"});
    dr.circles.push_back({{1240.0, 505.0}, 4.9, "Cavi"});
    dr.segments.push_back({{1020.0, 519.0}, {1020.0, 640.0}, "Bandierine"});
    dr.segments.push_back({{1020.0, 640.0}, {1060.0, 640.0}, "Bandierine"});
    dr.texts.push_back({"QGBT C-2 (4(1x95))", {1062.0, 639.0}, "Testi"});
    dr.segments.push_back({{1120.0, 515.0}, {1120.0, 620.0}, "Bandierine"});
    dr.segments.push_back({{1120.0, 620.0}, {1150.0, 620.0}, "Bandierine"});
    dr.texts.push_back({"QP1 C-3 Illuminazione corridoio", {1152.0, 619.0}, "Testi"});
    dr.texts.push_back({"IRAI C-4", {1200.0, 600.0}, "Testi"});
    dr.texts.push_back({"QE2-DX C-7", {1240.0, 590.0}, "Testi"});
    dr.texts.push_back({"CANALINA 75x300", {1000.0, 470.0}, "Titoli"});
    canalo::Tray t = canalo::readSection(dr);
    CHECK(approx(t.width, 300.0) && approx(t.height, 75.0));
    CHECK(t.cables.size() == 8);
    if (t.cables.size() != 8) return;
    canalo::recalculate(t);
    CHECK(t.cables[0].code == "QE2-DX C-7" && t.cables[0].form == "1(1x2,5)" && t.cables[0].type == "FS17" && t.cables[0].cores == 1 && t.cables[0].sys == "BT_POWER");
    CHECK(approx(t.cables[0].d, 6.8) && approx(t.cables[0].sec, 2.5) && approx(t.cables[0].x, 124.5) && approx(t.cables[0].y, 3.4));
    CHECK(t.cables[1].code == "IRAI C-4" && t.cables[1].form == "1(2x1,5)" && t.cables[1].type == "FTG18(O)M16" && t.cables[1].cores == 2 && t.cables[1].sys == "SPECIAL_ELV");
    CHECK(approx(t.cables[1].d, 9.8) && approx(t.cables[1].sec, 1.5) && approx(t.cables[1].x, 115.2) && approx(t.cables[1].y, 4.9));
    CHECK(t.cables[2].code == "QP1 C-3" && t.cables[2].form == "1(1x25)" && t.cables[2].type == "FG16M16" && t.cables[2].cores == 1 && t.cables[2].sys == "BT_POWER");
    CHECK(approx(t.cables[2].d, 12.2) && approx(t.cables[2].sec, 25.0) && approx(t.cables[2].x, 103.2) && approx(t.cables[2].y, 6.1));
    CHECK(t.cables[3].code == "QP1 C-3" && t.cables[3].form == "1(5G2,5)" && t.cables[3].type == "FG16OM16" && t.cables[3].cores == 3 && t.cables[3].sys == "BT_POWER");
    CHECK(approx(t.cables[3].d, 14.5) && approx(t.cables[3].sec, 2.5) && approx(t.cables[3].x, 89.3) && approx(t.cables[3].y, 7.3));
    CHECK(t.cables[4].code == "QGBT C-2" && t.cables[4].form == "1(1x95)" && t.cables[4].type == "FG16M16" && t.cables[4].cores == 1 && t.cables[4].sys == "BT_POWER");
    CHECK(approx(t.cables[4].d, 19.8) && approx(t.cables[4].sec, 95.0) && approx(t.cables[4].x, 10.4) && approx(t.cables[4].y, 9.9));
    CHECK(t.cables[5].code == "CAVO-6" && t.cables[5].form == "1(1x95)" && t.cables[5].type == "FG16M16" && t.cables[5].cores == 1 && t.cables[5].sys == "BT_POWER");
    CHECK(approx(t.cables[5].d, 19.8) && approx(t.cables[5].sec, 95.0) && approx(t.cables[5].x, 30.8) && approx(t.cables[5].y, 9.9));
    CHECK(t.cables[6].code == "CAVO-7" && t.cables[6].form == "1(1x95)" && t.cables[6].type == "FG16M16" && t.cables[6].cores == 1 && t.cables[6].sys == "BT_POWER");
    CHECK(approx(t.cables[6].d, 19.8) && approx(t.cables[6].sec, 95.0) && approx(t.cables[6].x, 51.2) && approx(t.cables[6].y, 9.9));
    CHECK(t.cables[7].code == "CAVO-8" && t.cables[7].form == "1(1x95)" && t.cables[7].type == "FG16M16" && t.cables[7].cores == 1 && t.cables[7].sys == "BT_POWER");
    CHECK(approx(t.cables[7].d, 19.8) && approx(t.cables[7].sec, 95.0) && approx(t.cables[7].x, 71.6) && approx(t.cables[7].y, 9.9));
    const canalo::Metrics m = canalo::recalculate(t);
    CHECK(approx(m.fillPct, 7.2) && approx(m.kr, 0.65) && approx(m.totalKgM, 10.88) && approx(m.maxPullDaN, 2090.0));
    CHECK(m.segDetail == "Art. 528.1 OK (Kr=0.65 su 1 str.)");
}

void canaloSection1()
{
    canalo::Drawn dr;
    dr.fileName = "Passerella_400x100.dxf";
    dr.circles.push_back({{50.0, 50.0}, 12.5, "Cavi"});
    dr.circles.push_back({{80.0, 50.0}, 12.5, "Cavi"});
    dr.circles.push_back({{110.0, 50.0}, 8.4, "Cavi"});
    dr.circles.push_back({{140.0, 50.0}, 8.4, "Cavi"});
    dr.circles.push_back({{170.0, 48.0}, 3.5, "Cavi"});
    dr.texts.push_back({"LINEA FM C-12", {60.0, 120.0}, "Testi"});
    dr.texts.push_back({"TD1 C-1", {140.0, 130.0}, "Bandierine"});
    canalo::Tray t = canalo::readSection(dr);
    CHECK(approx(t.width, 400.0) && approx(t.height, 100.0));
    CHECK(t.cables.size() == 5);
    if (t.cables.size() != 5) return;
    canalo::recalculate(t);
    CHECK(t.cables[0].code == "TD1 C-1" && t.cables[0].form == "1(1x4)" && t.cables[0].type == "FS17" && t.cables[0].cores == 1 && t.cables[0].sys == "BT_POWER");
    CHECK(approx(t.cables[0].d, 7.0) && approx(t.cables[0].sec, 4.0) && approx(t.cables[0].x, 90.4) && approx(t.cables[0].y, 3.5));
    CHECK(t.cables[1].code == "LINEA FM C-12" && t.cables[1].form == "1(5G16)" && t.cables[1].type == "FG16OM16" && t.cables[1].cores == 3 && t.cables[1].sys == "BT_POWER");
    CHECK(approx(t.cables[1].d, 25.0) && approx(t.cables[1].sec, 16.0) && approx(t.cables[1].x, 13.0) && approx(t.cables[1].y, 12.5));
    CHECK(t.cables[2].code == "CAVO-3" && t.cables[2].form == "1(5G16)" && t.cables[2].type == "FG16OM16" && t.cables[2].cores == 3 && t.cables[2].sys == "BT_POWER");
    CHECK(approx(t.cables[2].d, 25.0) && approx(t.cables[2].sec, 16.0) && approx(t.cables[2].x, 38.8) && approx(t.cables[2].y, 12.5));
    CHECK(t.cables[3].code == "CAVO-4" && t.cables[3].form == "1(5G4)" && t.cables[3].type == "FG16OM16" && t.cables[3].cores == 3 && t.cables[3].sys == "BT_POWER");
    CHECK(approx(t.cables[3].d, 16.8) && approx(t.cables[3].sec, 4.0) && approx(t.cables[3].x, 60.5) && approx(t.cables[3].y, 8.4));
    CHECK(t.cables[4].code == "CAVO-5" && t.cables[4].form == "1(5G4)" && t.cables[4].type == "FG16OM16" && t.cables[4].cores == 3 && t.cables[4].sys == "BT_POWER");
    CHECK(approx(t.cables[4].d, 16.8) && approx(t.cables[4].sec, 4.0) && approx(t.cables[4].x, 77.9) && approx(t.cables[4].y, 8.4));
    const canalo::Metrics m = canalo::recalculate(t);
    CHECK(approx(m.fillPct, 3.7) && approx(m.kr, 0.68) && approx(m.totalKgM, 13.08) && approx(m.maxPullDaN, 620.0));
    CHECK(m.segDetail == "Solo Cavi BT Potenza (Kr=0.68 su 1 str.)");
}

void testCanaloSections()
{
    canaloSection0();
    canaloSection1();
}
// clang-format on

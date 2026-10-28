"""Prepare all locales from the installed game's extracted locale tree."""
import pathlib, re, json, argparse
pattern=re.compile(r'@include\s+("(?:\\.|[^"\\])*")|\b(key|val)\[\]\s*:\s*("(?:\\.|[^"\\])*")')
def unquote(value):
    try:return json.loads(value)
    except json.JSONDecodeError:return re.sub(r'\\(x[0-9a-fA-F]{2}|[nrt"\\])',lambda m:chr(int(m[1][1:],16)) if m[1].startswith('x') else {'n':'\n','r':'\r','t':'\t','"':'"','\\':'\\'}[m[1]],value[1:-1])
def load(path,result,source,stack=()):
    path=path.resolve()
    if not path.is_file():return
    if path in stack:raise ValueError('include cycle')
    key=None
    for match in pattern.finditer(path.read_text(encoding='utf-8-sig')):
        if match[1]:
            name=unquote(match[1]);load(source/name.lstrip('/') if name.startswith('/') else path.parent/name,result,source,stack+(path,))
        elif match[2]=='key':key=unquote(match[3])
        elif key is not None:result[key]=unquote(match[3]);key=None
def write(path,values):
    path.write_text(''.join(k+'\t'+v.replace('\t',' ').replace('\r',' ').replace('\n',' ')+'\n' for k,v in sorted(values.items())),encoding='utf8')
# Custom controls. Shared labels and gameplay messages use native game keys.
# Columns: title | auto trailer | nearby company | immediate pickup | create | surprise | offer added | route unavailable.
rows={
'en_gb':'Job Dispatcher|Auto-compatible|Nearby company|Take job immediately|Create job|Surprise me|Job added. Visit the company and select it at the green marker.|No route is available between these depots.',
'ru_ru':'Диспетчер грузов|Автоматически|Ближайшая компания|Поместить груз сразу|Создать задание|Удиви меня|Заказ добавлен. Подъезжайте в компанию и выберите его на зелёном значке.|Между выбранными площадками нет доступного маршрута.',
'bg_bg':'Диспечер на товари|Автоматично|Близка фирма|Вземи товара веднага|Създай поръчка|Изненадай ме|Поръчката е добавена. Изберете я при зеления знак във фирмата.|Няма маршрут между тези бази.',
'ca_es':'Despatx de càrregues|Automàtic|Empresa propera|Accepta la feina ara|Crea una feina|Sorprèn-me|Feina afegida. Selecciona-la al marcador verd de l’empresa.|No hi ha cap ruta entre aquests centres.',
'cs_cz':'Dispečer nákladů|Automaticky|Blízká firma|Přijmout zakázku ihned|Vytvořit zakázku|Překvap mě|Zakázka přidána. Vyberte ji u zelené značky ve firmě.|Mezi těmito provozovnami není dostupná trasa.',
'da_dk':'Fragtdisponent|Automatisk|Firma i nærheden|Tag jobbet med det samme|Opret job|Overrask mig|Jobbet er tilføjet. Vælg det ved firmaets grønne markør.|Der er ingen rute mellem disse depoter.',
'de_de':'Fracht-Dispatcher|Automatisch|Firma in der Nähe|Auftrag sofort annehmen|Auftrag erstellen|Überrasch mich|Auftrag hinzugefügt. Wähle ihn am grünen Symbol der Firma.|Zwischen diesen Betriebshöfen ist keine Route verfügbar.',
'el_gr':'Διαχείριση φορτίων|Αυτόματα|Κοντινή εταιρεία|Άμεση αποδοχή εργασίας|Δημιουργία εργασίας|Κάνε μου έκπληξη|Η εργασία προστέθηκε. Επίλεξέ την στο πράσινο σημείο της εταιρείας.|Δεν υπάρχει διαδρομή μεταξύ αυτών των εγκαταστάσεων.',
'es_es':'Despachador de cargas|Automático|Empresa cercana|Aceptar trabajo ahora|Crear trabajo|Sorpréndeme|Trabajo añadido. Selecciónalo en el marcador verde de la empresa.|No hay ninguna ruta entre estas instalaciones.',
'et_ee':'Veoste dispetšer|Automaatselt|Lähedal asuv ettevõte|Võta töö kohe vastu|Loo töö|Üllata mind|Töö lisatud. Vali see ettevõtte rohelise märgi juures.|Nende ladude vahel pole marsruuti.',
'eu_es':'Karga-banatzailea|Automatikoki|Hurbileko enpresa|Onartu lana orain|Sortu lana|Harritu nazazu|Lana gehitu da. Hautatu enpresako marka berdean.|Ez dago ibilbiderik kokaleku horien artean.',
'fi_fi':'Rahtipäivystäjä|Automaattinen|Läheinen yritys|Ota työ heti vastaan|Luo työ|Yllätä minut|Työ lisätty. Valitse se yrityksen vihreällä merkillä.|Näiden toimipaikkojen välillä ei ole reittiä.',
'fr_fr':'Répartiteur de fret|Automatique|Entreprise proche|Accepter la mission maintenant|Créer une mission|Surprends-moi|Mission ajoutée. Sélectionnez-la au repère vert de l’entreprise.|Aucun itinéraire entre ces dépôts.',
'gl_es':'Despachador de cargas|Automático|Empresa próxima|Aceptar traballo agora|Crear traballo|Sorpréndeme|Traballo engadido. Escólleo no sinal verde da empresa.|Non hai ruta entre estas instalacións.',
'hr_hr':'Dispečer tereta|Automatski|Obližnja tvrtka|Odmah prihvati posao|Stvori posao|Iznenadi me|Posao je dodan. Odaberite ga na zelenoj oznaci u tvrtki.|Nema dostupne rute između ovih lokacija.',
'hu_hu':'Fuvarszervező|Automatikus|Közeli vállalat|Fuvar azonnali elfogadása|Fuvar létrehozása|Lepj meg|A fuvar hozzáadva. Válaszd ki a vállalat zöld jelzésénél.|Nincs útvonal a két telephely között.',
'it_it':'Gestione carichi|Automatico|Azienda vicina|Accetta subito il lavoro|Crea lavoro|Sorprendimi|Lavoro aggiunto. Selezionalo presso il simbolo verde dell’azienda.|Nessun percorso disponibile tra queste sedi.',
'ja_jp':'貨物配車|自動選択|近くの会社|すぐに仕事を受ける|仕事を作成|おまかせ|仕事を追加しました。会社の緑色のマーカーで選択してください。|この拠点間のルートが見つかりません。',
'ka_ge':'ტვირთის დისპეტჩერი|ავტომატურად|ახლომდებარე კომპანია|შეკვეთის დაუყოვნებლივ მიღება|შეკვეთის შექმნა|გამაოცე|შეკვეთა დამატებულია. აირჩიეთ ის კომპანიის მწვანე ნიშნულთან.|ამ ობიექტებს შორის მარშრუტი არ არის.',
'ko_kr':'화물 배차|자동 선택|가까운 회사|즉시 작업 수락|작업 만들기|무작위 추천|작업이 추가되었습니다. 회사의 녹색 표시에서 선택하세요.|이 사업장 사이의 경로를 찾을 수 없습니다.',
'lt_lt':'Krovinių dispečeris|Automatiškai|Artima įmonė|Priimti darbą iš karto|Sukurti darbą|Nustebink mane|Darbas pridėtas. Pasirinkite jį prie įmonės žalio ženklo.|Tarp šių aikštelių nėra maršruto.',
'lv_lv':'Kravu dispečers|Automātiski|Tuvumā esošs uzņēmums|Pieņemt darbu uzreiz|Izveidot darbu|Pārsteidz mani|Darbs pievienots. Izvēlieties to pie uzņēmuma zaļās zīmes.|Starp šīm vietām nav pieejama maršruta.',
'mk_mk':'Диспечер за товар|Автоматски|Блиска компанија|Прифати ја работата веднаш|Создај работа|Изненади ме|Работата е додадена. Изберете ја кај зелениот знак во компанијата.|Нема достапна рута меѓу овие локации.',
'nl_nl':'Vrachtdispatcher|Automatisch|Bedrijf in de buurt|Opdracht direct aannemen|Opdracht maken|Verras me|Opdracht toegevoegd. Kies deze bij het groene pictogram van het bedrijf.|Er is geen route tussen deze vestigingen.',
'no_no':'Fraktdisponent|Automatisk|Firma i nærheten|Ta oppdraget med en gang|Opprett oppdrag|Overrask meg|Oppdraget er lagt til. Velg det ved firmaets grønne markør.|Det finnes ingen rute mellom disse stedene.',
'pl_pl':'Dyspozytor ładunków|Automatycznie|Pobliska firma|Przyjmij zlecenie od razu|Utwórz zlecenie|Zaskocz mnie|Zlecenie dodane. Wybierz je przy zielonym znaczniku firmy.|Brak dostępnej trasy między tymi placówkami.',
'pl_si':'Dyspozytōr ładōnkōw|Autōmatycznie|Poblisko firma|Weź zlecenie ôd razu|Stwōrz zlecenie|Zaskocz mie|Zlecenie dodane. Ôbier je przi zielōnym znaku firmy.|Niy ma trasy miyndzy tymi placami.',
'pt_br':'Despachante de cargas|Automático|Empresa próxima|Aceitar trabalho imediatamente|Criar trabalho|Surpreenda-me|Trabalho adicionado. Selecione-o no marcador verde da empresa.|Não há rota disponível entre esses locais.',
'pt_pt':'Despachante de cargas|Automático|Empresa próxima|Aceitar trabalho imediatamente|Criar trabalho|Surpreende-me|Trabalho adicionado. Seleciona-o no marcador verde da empresa.|Não existe rota disponível entre estes locais.',
'ro_ro':'Dispecer de mărfuri|Automat|Companie apropiată|Acceptă cursa imediat|Creează cursă|Surprinde-mă|Cursa a fost adăugată. Selecteaz-o la marcajul verde al companiei.|Nu există o rută între aceste locații.',
'sk_sk':'Dispečer nákladov|Automaticky|Blízka firma|Prijať zákazku ihneď|Vytvoriť zákazku|Prekvap ma|Zákazka bola pridaná. Vyberte ju pri zelenej značke vo firme.|Medzi týmito prevádzkami nie je dostupná trasa.',
'sl_sl':'Dispečer tovora|Samodejno|Bližnje podjetje|Takoj sprejmi delo|Ustvari delo|Preseneti me|Delo je dodano. Izberite ga pri zelenem znaku podjetja.|Med tema lokacijama ni razpoložljive poti.',
'sr_sp':'Dispečer tereta|Automatski|Obližnja kompanija|Odmah prihvati posao|Napravi posao|Iznenadi me|Posao je dodat. Izaberite ga kod zelenog znaka u kompaniji.|Nema dostupne rute između ovih lokacija.',
'sr_sr':'Диспечер терета|Аутоматски|Оближња компанија|Одмах прихвати посао|Направи посао|Изненади ме|Посао је додат. Изаберите га код зеленог знака у компанији.|Нема доступне руте између ових локација.',
'sv_se':'Fraktdisponent|Automatiskt|Närliggande företag|Ta jobbet direkt|Skapa jobb|Överraska mig|Jobbet har lagts till. Välj det vid företagets gröna markör.|Det finns ingen rutt mellan dessa depåer.',
'tr_tr':'Yük sevkiyatı|Otomatik|Yakındaki şirket|İşi hemen kabul et|İş oluştur|Beni şaşırt|İş eklendi. Şirketin yeşil işaretinden seçin.|Bu tesisler arasında kullanılabilir rota yok.',
'uk_uk':'Диспетчер вантажів|Автоматично|Найближча компанія|Розмістити вантаж одразу|Створити замовлення|Здивуй мене|Замовлення додано. Виберіть його на зеленому значку компанії.|Між вибраними майданчиками немає доступного маршруту.',
'vi_vn':'Điều phối hàng hóa|Tự động|Công ty gần đây|Nhận việc ngay|Tạo công việc|Làm tôi bất ngờ|Đã thêm công việc. Hãy chọn tại biểu tượng màu xanh ở công ty.|Không có tuyến đường giữa hai địa điểm này.',
'zh_cn':'货物调度|自动选择|附近公司|立即接受任务|创建任务|随机推荐|任务已添加。请前往公司的绿色标记处选择。|这两个场站之间没有可用路线。',
'zh_tw':'貨物調度|自動選擇|附近公司|立即接受任務|建立任務|隨機推薦|任務已新增。請前往公司的綠色標記處選擇。|這兩個場站之間沒有可用路線。',
}
for alias,base in {'en_us':'en_gb','es_la':'es_es','fr_ca':'fr_fr'}.items():rows[alias]=rows[base]
errors={
'en_gb':'Stop the truck and return to driving.|Detach the current trailer first.|This cargo does not fit the selected trailer.',
'ru_ru':'Остановитесь и вернитесь в кабину.|Сначала отцепите текущий прицеп.|Этот груз не подходит к выбранному прицепу.',
'bg_bg':'Спрете камиона и се върнете в кабината.|Първо откачете ремаркето.|Този товар не е подходящ за избраното ремарке.',
'ca_es':'Atura el camió i torna a la cabina.|Desenganxa primer el remolc.|Aquesta càrrega no és compatible amb el remolc seleccionat.',
'cs_cz':'Zastavte a vraťte se do kabiny.|Nejprve odpojte přívěs.|Náklad se nehodí pro vybraný přívěs.',
'da_dk':'Stop lastbilen, og vend tilbage til førerhuset.|Frakobl først anhængeren.|Lasten passer ikke til den valgte anhænger.',
'de_de':'Halte an und kehre ins Fahrerhaus zurück.|Kupple zuerst den Anhänger ab.|Diese Fracht passt nicht zum gewählten Anhänger.',
'el_gr':'Σταμάτησε το φορτηγό και επέστρεψε στην καμπίνα.|Αποσύνδεσε πρώτα το ρυμουλκούμενο.|Το φορτίο δεν ταιριάζει στο επιλεγμένο ρυμουλκούμενο.',
'es_es':'Detén el camión y vuelve a la cabina.|Desengancha primero el remolque.|La carga no es compatible con el remolque seleccionado.',
'et_ee':'Peata veok ja naase kabiini.|Haagi kõigepealt haagis lahti.|See veos ei sobi valitud haagisele.',
'eu_es':'Gelditu kamioia eta itzuli kabinara.|Askatu lehenik atoia.|Karga ez da bateragarria hautatutako atoiarekin.',
'fi_fi':'Pysäytä kuorma-auto ja palaa ohjaamoon.|Irrota ensin perävaunu.|Lasti ei sovi valittuun perävaunuun.',
'fr_fr':'Arrêtez le camion et retournez en cabine.|Détachez d’abord la remorque.|Cette cargaison ne convient pas à la remorque choisie.',
'gl_es':'Detén o camión e volve á cabina.|Desengancha primeiro o remolque.|A carga non é compatible co remolque seleccionado.',
'hr_hr':'Zaustavite kamion i vratite se u kabinu.|Prvo odvojite prikolicu.|Teret ne odgovara odabranoj prikolici.',
'hu_hu':'Állítsd meg a kamiont, és térj vissza a fülkébe.|Először kapcsold le a pótkocsit.|A rakomány nem fér a kiválasztott pótkocsira.',
'it_it':'Ferma il camion e torna in cabina.|Sgancia prima il rimorchio.|Il carico non è adatto al rimorchio scelto.',
'ja_jp':'トラックを停止して運転席に戻ってください。|先にトレーラーを切り離してください。|この貨物は選択したトレーラーに適合しません。',
'ka_ge':'გააჩერეთ სატვირთო და დაბრუნდით კაბინაში.|ჯერ მოხსენით მისაბმელი.|ტვირთი არ შეესაბამება არჩეულ მისაბმელს.',
'ko_kr':'트럭을 멈추고 운전석으로 돌아가세요.|먼저 트레일러를 분리하세요.|선택한 트레일러에 맞지 않는 화물입니다.',
'lt_lt':'Sustabdykite sunkvežimį ir grįžkite į kabiną.|Pirmiausia atkabinkite priekabą.|Krovinys netinka pasirinktai priekabai.',
'lv_lv':'Apturiet kravas auto un atgriezieties kabīnē.|Vispirms atvienojiet piekabi.|Krava nav piemērota izvēlētajai piekabei.',
'mk_mk':'Запрете го камионот и вратете се во кабината.|Прво откачете ја приколката.|Товарот не одговара на избраната приколка.',
'nl_nl':'Stop de truck en keer terug naar de cabine.|Koppel eerst de aanhanger af.|Deze lading past niet in de gekozen aanhanger.',
'no_no':'Stopp lastebilen og gå tilbake til førerhuset.|Koble fra tilhengeren først.|Lasten passer ikke til den valgte tilhengeren.',
'pl_pl':'Zatrzymaj ciężarówkę i wróć do kabiny.|Najpierw odłącz naczepę.|Ładunek nie pasuje do wybranej naczepy.',
'pl_si':'Zatrzimej ciynżarōwka i wrōć do kabiny.|Naprzōd ôdepnij naczepa.|Ładōnek niy pasuje do ôbranyj naczepy.',
'pt_br':'Pare o caminhão e volte à cabine.|Desengate primeiro o reboque.|A carga não é compatível com o reboque selecionado.',
'pt_pt':'Para o camião e regressa à cabina.|Desengata primeiro o reboque.|A carga não é compatível com o reboque selecionado.',
'ro_ro':'Oprește camionul și revino în cabină.|Decuplează mai întâi remorca.|Marfa nu este compatibilă cu remorca selectată.',
'sk_sk':'Zastavte kamión a vráťte sa do kabíny.|Najprv odpojte príves.|Náklad nie je vhodný pre vybraný príves.',
'sl_sl':'Ustavite tovornjak in se vrnite v kabino.|Najprej odklopite prikolico.|Tovor ne ustreza izbrani prikolici.',
'sr_sp':'Zaustavite kamion i vratite se u kabinu.|Prvo otkačite prikolicu.|Teret ne odgovara izabranoj prikolici.',
'sr_sr':'Зауставите камион и вратите се у кабину.|Прво откачите приколицу.|Терет не одговара изабраној приколици.',
'sv_se':'Stanna lastbilen och återgå till hytten.|Koppla loss släpet först.|Lasten passar inte det valda släpet.',
'tr_tr':'Kamyonu durdurun ve kabine dönün.|Önce römorku ayırın.|Bu yük seçilen römorka uygun değil.',
'uk_uk':'Зупиніться та поверніться до кабіни.|Спочатку відчепіть поточний причіп.|Цей вантаж не підходить до вибраного причепа.',
'vi_vn':'Dừng xe tải và quay lại buồng lái.|Hãy tháo rơ moóc trước.|Hàng không phù hợp với rơ moóc đã chọn.',
'zh_cn':'请停车并返回驾驶室。|请先断开当前挂车。|所选挂车不适合此货物。',
'zh_tw':'請停車並返回駕駛室。|請先斷開目前的拖車。|所選拖車不適合此貨物。',
}
for alias,base in {'en_us':'en_gb','es_la':'es_es','fr_ca':'fr_fr'}.items():errors[alias]=errors[base]
keys=['jd_title','jd_auto','jd_nearby','jd_immediate','jd_create','jd_surprise','jd_offer','jd_route_failed']
real_time={
'en_gb':'Estimated real time','ru_ru':'Оценка реального времени','bg_bg':'Приблизително реално време','ca_es':'Temps real estimat',
'cs_cz':'Odhad skutečného času','da_dk':'Anslået realtid','de_de':'Geschätzte Echtzeit','el_gr':'Εκτιμώμενος πραγματικός χρόνος',
'es_es':'Tiempo real estimado','et_ee':'Hinnanguline tegelik aeg','eu_es':'Benetako denbora estimatua','fi_fi':'Arvioitu todellinen aika',
'fr_fr':'Durée réelle estimée','gl_es':'Tempo real estimado','hr_hr':'Procijenjeno stvarno vrijeme','hu_hu':'Becsült valós idő',
'it_it':'Tempo reale stimato','ja_jp':'実時間の目安','ka_ge':'სავარაუდო რეალური დრო','ko_kr':'예상 실제 시간',
'lt_lt':'Numatomas tikrasis laikas','lv_lv':'Aptuvenais reālais laiks','mk_mk':'Проценето реално време','nl_nl':'Geschatte werkelijke tijd',
'no_no':'Anslått virkelig tid','pl_pl':'Szacowany czas rzeczywisty','pl_si':'Ôszacowany rzeczywisty czas','pt_br':'Tempo real estimado',
'pt_pt':'Tempo real estimado','ro_ro':'Timp real estimat','sk_sk':'Odhad skutočného času','sl_sl':'Ocenjeni dejanski čas',
'sr_sp':'Procenjeno stvarno vreme','sr_sr':'Процењено стварно време','sv_se':'Beräknad verklig tid','tr_tr':'Tahmini gerçek süre',
'uk_uk':'Оцінка реального часу','vi_vn':'Thời gian thực ước tính','zh_cn':'预计实际时间','zh_tw':'預計實際時間'}
for alias,base in {'en_us':'en_gb','es_la':'es_es','fr_ca':'fr_fr'}.items():real_time[alias]=real_time[base]
def locale_values(source,directory):
    values={}
    load(directory/'local.sii',values,source)
    load(directory/'local.override.sii',values,source)
    mod_locales=source.parent/'mod-files'
    if mod_locales.is_dir():
        for pack in sorted(mod_locales.iterdir()):
            for alias in ({'fr_ca':['fr_qc']}.get(directory.name,[])+[directory.name]):
                for path in sorted((pack/'locale'/alias).glob('*')):
                    if path.suffix in ('.sii','.sui'):load(path,values,source)
    return values


def write_language(source,output,directory):
    language=directory.name
    values=locale_values(source,directory)
    custom=dict(zip(keys,rows[language].split('|'),strict=True))
    custom.update(zip(('jd_stop','jd_detach','jd_incompatible'),errors[language].split('|'),strict=True))
    custom['jd_real_time']=real_time[language]
    if language=='ru_ru':custom['cargo_waiting1']='Груз сгенерирован. Можете забирать его в выбранном месте погрузки.'
    write(output/f'game-labels.{language}.tsv',values)
    write(output/f'ui-labels.{language}.tsv',custom)
    return {'native_keys':len(values),'custom_keys':len(custom)}


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=pathlib.Path,required=True)
    parser.add_argument('--output',type=pathlib.Path,required=True)
    args=parser.parse_args(argv)
    args.output.mkdir(parents=True,exist_ok=True)
    coverage={}
    for directory in sorted((args.source/'locale').iterdir()):
        if directory.is_dir() and directory.name in rows:
            coverage[directory.name]=write_language(args.source,args.output,directory)
    (args.output/'locales-manifest.json').write_text(json.dumps(coverage,ensure_ascii=False,indent=2),encoding='utf8')
    print('Prepared',len(coverage),'game locales')


if __name__=='__main__':
    main()

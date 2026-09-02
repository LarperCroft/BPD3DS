#include <3ds.h>
#include <citro2d.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>

// Error on startup handler
static void showStartupError(
    const char* message
) {
    consoleInit(
        GFX_TOP,
        NULL
    );

    consoleClear();

    printf(
        "\x1b[2;2HBPD3DS STARTUP ERROR\n\n"
    );

    printf(
        "%s\n\n",
        message
    );

    printf(
        "Press START to exit."
    );

    while (
        aptMainLoop()
    ) {
        hidScanInput();

        if (
            hidKeysDown() & KEY_START
        ) {
            break;
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
}


typedef struct {
    const char* category;
    const char* title;
    const char* advice;
} BPDCard;


static const char* BPD_CARDS_FILE =
    "romfs:/BPDCardDeck.txt";

static const char* FAVOURITES_FILE =
    "sdmc:/3ds/BPD3DS/favourites.dat";

static const int MAX_CARDS = 1000;

static const int MAX_CATEGORY_LENGTH = 64;

static const int MAX_TITLE_LENGTH = 80;

static const int MAX_ADVICE_LENGTH = 600;

static const int MAX_LINE_LENGTH = 768;

static const int MAX_CATEGORIES = 32;

static C3D_RenderTarget* topTarget = NULL;

static C3D_RenderTarget* bottomTarget = NULL;

static C2D_Font systemFont;

static C2D_TextBuf textBuffer;

static C2D_Text titleText;

static C2D_Text categoryText;

static C2D_Text adviceText;

static C2D_Text favouriteText;

static C2D_Text buttonTexts[MAX_CATEGORIES + 2];


// ============================================================
// COLOURS
// ============================================================

static const u32 COLOR_BACKGROUND =
    C2D_Color32(18, 18, 24, 255);

static const u32 COLOR_PANEL =
    C2D_Color32(35, 35, 45, 255);

static const u32 COLOR_BUTTON =
    C2D_Color32(50, 50, 65, 255);

static const u32 COLOR_BUTTON_SELECTED =
    C2D_Color32(75, 65, 105, 255);

static const u32 COLOR_BUTTON_PRESSED =
    C2D_Color32(100, 85, 135, 255);

static const u32 COLOR_TEXT =
    C2D_Color32(245, 245, 250, 255);

static const u32 COLOR_SECONDARY =
    C2D_Color32(185, 185, 200, 255);

static const u32 COLOR_ACCENT =
    C2D_Color32(150, 120, 190, 255);

static const u32 COLOR_FAVOURITE =
    C2D_Color32(230, 190, 90, 255);


// ============================================================
// BOTTOM SCREEN BUTTON LAYOUT
// ============================================================


static const float BUTTON_LEFT = 10.0f;

static const float BUTTON_RIGHT = 310.0f;

static const float BUTTON_START_Y = 31.0f;

static const float BUTTON_HEIGHT = 25.0f;

static const float BUTTON_GAP = 2.0f;


// ============================================================
// CARD STORAGE
// ============================================================

static BPDCard cards[MAX_CARDS];

static char cardCategory[
    MAX_CARDS
][MAX_CATEGORY_LENGTH];

static char cardTitle[
    MAX_CARDS
][MAX_TITLE_LENGTH];

static char cardAdvice[
    MAX_CARDS
][MAX_ADVICE_LENGTH];

static bool favourites[MAX_CARDS] = {};

static int cardCount = 0;

static int currentCard = 0;


// ============================================================
// CATEGORY STORAGE
// ============================================================

static char categoryNames[
    MAX_CATEGORIES
][MAX_CATEGORY_LENGTH];

static int categoryCount = 0;

static int currentCategory = 0;


// ============================================================
// UI STATE
// ============================================================

static bool showingFavourites = false;

static bool showingDailyCard = true;

static bool showingHelp = false;

static int pressedButton = -1;


// ============================================================
// UTILITY FUNCTIONS
// ============================================================

static void copyString(
    char* destination,
    int destinationSize,
    const char* source
) {
    if (
        destination == NULL ||
        source == NULL ||
        destinationSize <= 0
    ) {
        return;
    }

    int length = strlen(source);

    if (length >= destinationSize) {
        length = destinationSize - 1;
    }

    memcpy(
        destination,
        source,
        length
    );

    destination[length] = '\0';
}


// ------------------------------------------------------------
// Trim whitespace
// ------------------------------------------------------------

static void trimWhitespace(
    char* text
) {
    if (text == NULL) {
        return;
    }

    char* start = text;

    while (
        *start == ' ' ||
        *start == '\t'
    ) {
        start++;
    }

    if (start != text) {
        memmove(
            text,
            start,
            strlen(start) + 1
        );
    }

    int length = strlen(text);

    while (
        length > 0 &&
        (
            text[length - 1] == ' ' ||
            text[length - 1] == '\t'
        )
    ) {
        text[length - 1] = '\0';
        length--;
    }
}


// ------------------------------------------------------------
// Convert unsupported UTF-8 characters
// ------------------------------------------------------------

static void convertUnsupportedCharacters(
    char* text
) {
    if (text == NULL) {
        return;
    }

    char* source = text;

    char* destination = text;

    while (*source != '\0') {

        unsigned char first =
            (unsigned char)source[0];

        unsigned char second =
            (unsigned char)source[1];

        unsigned char third =
            (unsigned char)source[2];


        // ----------------------------------------------------
        // Left/right single quotation marks
        // ----------------------------------------------------

        if (
            first == 0xE2 &&
            second == 0x80 &&
            (
                third == 0x98 ||
                third == 0x99
            )
        ) {
            *destination++ = '\'';
            source += 3;
        }

        // ----------------------------------------------------
        // En dash / em dash
        // ----------------------------------------------------

        else if (
            first == 0xE2 &&
            second == 0x80 &&
            (
                third == 0x93 ||
                third == 0x94
            )
        ) {
            *destination++ = '-';
            source += 3;
        }

        // ----------------------------------------------------
        // Left/right double quotation marks
        // ----------------------------------------------------

        else if (
            first == 0xE2 &&
            second == 0x80 &&
            (
                third == 0x9C ||
                third == 0x9D
            )
        ) {
            *destination++ = '"';
            source += 3;
        }

        // ----------------------------------------------------
        // Ellipsis
        // ----------------------------------------------------

        else if (
            first == 0xE2 &&
            second == 0x80 &&
            third == 0xA6
        ) {
            *destination++ = '.';
            *destination++ = '.';
            *destination++ = '.';
            source += 3;
        }

        else {
            *destination++ = *source++;
        }
    }

    *destination = '\0';
}


// ------------------------------------------------------------
// Case-insensitive comparison
// ------------------------------------------------------------

static bool stringsEqualIgnoreCase(
    const char* first,
    const char* second
) {
    if (
        first == NULL ||
        second == NULL
    ) {
        return false;
    }

    while (
        *first != '\0' &&
        *second != '\0'
    ) {

        if (
            tolower(
                (unsigned char)*first
            )
            !=
            tolower(
                (unsigned char)*second
            )
        ) {
            return false;
        }

        first++;
        second++;
    }

    return (
        *first == '\0' &&
        *second == '\0'
    );
}


// ============================================================
// CATEGORY MANAGEMENT
// ============================================================

static int findCategory(
    const char* category
) {
    if (category == NULL) {
        return -1;
    }

    for (
        int i = 0;
        i < categoryCount;
        i++
    ) {

        if (
            stringsEqualIgnoreCase(
                categoryNames[i],
                category
            )
        ) {
            return i;
        }
    }

    return -1;
}

static bool addCategory(
    const char* category
) {
    if (
        category == NULL ||
        category[0] == '\0'
    ) {
        return false;
    }

    if (
        findCategory(category) >= 0
    ) {
        return true;
    }

    if (
        categoryCount >= MAX_CATEGORIES
    ) {
        return false;
    }

    copyString(
        categoryNames[categoryCount],
        MAX_CATEGORY_LENGTH,
        category
    );

    categoryCount++;

    return true;
}


// ============================================================
// LOAD CARDS
// ============================================================

static bool loadCards() {

    FILE* file =
        fopen(
            BPD_CARDS_FILE,
            "r"
        );

    if (file == NULL) {
        return false;
    }

    char line[
        MAX_LINE_LENGTH
    ];

    while (
        cardCount < MAX_CARDS &&
        fgets(
            line,
            sizeof(line),
            file
        ) != NULL
    ) {

        line[
            strcspn(
                line,
                "\r\n"
            )
        ] = '\0';


        if (line[0] == '\0') {
            continue;
        }


        // ----------------------------------------------------
        // First separator
        // ----------------------------------------------------

        char* firstSeparator =
            strchr(
                line,
                '|'
            );


        if (
            firstSeparator == NULL
        ) {
            continue;
        }


        *firstSeparator = '\0';


        // ----------------------------------------------------
        // Second separator
        // ----------------------------------------------------

        char* secondSeparator =
            strchr(
                firstSeparator + 1,
                '|'
            );


        if (
            secondSeparator == NULL
        ) {
            continue;
        }


        *secondSeparator = '\0';


        char* category =
            line;

        char* title =
            firstSeparator + 1;

        char* advice =
            secondSeparator + 1;


        trimWhitespace(category);

        trimWhitespace(title);

        trimWhitespace(advice);


        convertUnsupportedCharacters(
            category
        );

        convertUnsupportedCharacters(
            title
        );

        convertUnsupportedCharacters(
            advice
        );


        if (
            category[0] == '\0' ||
            title[0] == '\0' ||
            advice[0] == '\0'
        ) {
            continue;
        }


        copyString(
            cardCategory[cardCount],
            MAX_CATEGORY_LENGTH,
            category
        );


        copyString(
            cardTitle[cardCount],
            MAX_TITLE_LENGTH,
            title
        );


        copyString(
            cardAdvice[cardCount],
            MAX_ADVICE_LENGTH,
            advice
        );


        cards[cardCount].category =
            cardCategory[cardCount];

        cards[cardCount].title =
            cardTitle[cardCount];

        cards[cardCount].advice =
            cardAdvice[cardCount];


        addCategory(category);


        cardCount++;
    }


    fclose(file);


    return cardCount > 0;
}


// ============================================================
// FAVOURITES
// ============================================================

static void loadFavourites() {

    FILE* file =
        fopen(
            FAVOURITES_FILE,
            "rb"
        );


    if (file == NULL) {

        memset(
            favourites,
            0,
            sizeof(favourites)
        );

        return;
    }


    size_t valuesRead =
        fread(
            favourites,
            sizeof(favourites[0]),
            cardCount,
            file
        );


    fclose(file);


    if (
        valuesRead !=
        (size_t)cardCount
    ) {

        memset(
            favourites,
            0,
            sizeof(favourites)
        );
    }
}


// Writes saves to SD card
static void saveFavourites() {

    int directoryResult;


    directoryResult =
        mkdir(
            "sdmc:/3ds",
            0777
        );


    if (
        directoryResult != 0 &&
        errno != EEXIST
    ) {
        return;
    }


    directoryResult =
        mkdir(
            "sdmc:/3ds/BPD3DS",
            0777
        );


    if (
        directoryResult != 0 &&
        errno != EEXIST
    ) {
        return;
    }


    FILE* file =
        fopen(
            FAVOURITES_FILE,
            "wb"
        );


    if (file == NULL) {
        return;
    }


    size_t valuesWritten =
        fwrite(
            favourites,
            sizeof(favourites[0]),
            cardCount,
            file
        );


    fclose(file);


    if (
        valuesWritten !=
        (size_t)cardCount
    ) {
        return;
    }
}


// ============================================================
// FILTERING
// ============================================================

static bool cardMatchesCurrentFilter(
    int index
) {

    if (
        index < 0 ||
        index >= cardCount
    ) {
        return false;
    }


    if (showingFavourites) {
        return favourites[index];
    }


    if (currentCategory == 0) {
        return true;
    }


    return stringsEqualIgnoreCase(
        cards[index].category,
        categoryNames[
            currentCategory - 1
        ]
    );
}


// ============================================================
// GET MATCHING CARDS
// ============================================================

static int getMatchingCards(
    int* matchingIndexes
) {

    if (matchingIndexes == NULL) {
        return 0;
    }


    int matchingCount = 0;


    for (
        int i = 0;
        i < cardCount;
        i++
    ) {

        if (
            cardMatchesCurrentFilter(i)
        ) {

            matchingIndexes[
                matchingCount
            ] = i;

            matchingCount++;
        }
    }


    return matchingCount;
}


// ============================================================
// FAVOURITES CHECK
// ============================================================

static bool hasFavourites() {

    for (
        int i = 0;
        i < cardCount;
        i++
    ) {

        if (favourites[i]) {
            return true;
        }
    }


    return false;
}


// ============================================================
// RANDOM CARD
// ============================================================

static void chooseCard() {

    int matchingIndexes[
        MAX_CARDS
    ];


    int matchingCount =
        getMatchingCards(
            matchingIndexes
        );


    if (
        matchingCount == 0
    ) {
        return;
    }


    int selectedPosition = 0;


    if (
        matchingCount > 1
    ) {

        do {

            selectedPosition =
                rand() % matchingCount;

        } while (
            matchingIndexes[
                selectedPosition
            ]
            ==
            currentCard
        );
    }


    currentCard =
        matchingIndexes[
            selectedPosition
        ];


    showingDailyCard = false;
}


// ============================================================
// DAILY CARD
// ============================================================

static void chooseDailyCard() {

    int matchingIndexes[
        MAX_CARDS
    ];


    int matchingCount =
        getMatchingCards(
            matchingIndexes
        );


    if (
        matchingCount == 0
    ) {
        return;
    }


    time_t currentTime =
        time(NULL);


    struct tm* localTime =
        localtime(
            &currentTime
        );


    if (localTime == NULL) {

        chooseCard();

        showingDailyCard = true;

        return;
    }


    int dayKey =
        (
            (localTime->tm_year + 1900)
            * 366
        )
        +
        localTime->tm_yday;


    currentCard =
        matchingIndexes[
            dayKey % matchingCount
        ];


    showingDailyCard = true;
}


// ============================================================
// BROWSE
// ============================================================

static void browseCards(
    int direction
) {

    int matchingIndexes[
        MAX_CARDS
    ];


    int matchingCount =
        getMatchingCards(
            matchingIndexes
        );


    if (
        matchingCount == 0
    ) {
        return;
    }


    int currentPosition = 0;


    for (
        int i = 0;
        i < matchingCount;
        i++
    ) {

        if (
            matchingIndexes[i]
            ==
            currentCard
        ) {

            currentPosition = i;

            break;
        }
    }


    currentPosition += direction;


    if (
        currentPosition < 0
    ) {

        currentPosition =
            matchingCount - 1;
    }


    if (
        currentPosition >=
        matchingCount
    ) {

        currentPosition = 0;
    }


    currentCard =
        matchingIndexes[
            currentPosition
        ];


    showingDailyCard = false;
}


// ============================================================
// CATEGORY SELECTION
// ============================================================

static bool selectFirstCardInCategory(
    int categoryIndex
) {

    if (
        categoryIndex < 0 ||
        categoryIndex > categoryCount
    ) {
        return false;
    }


    // --------------------------------------------------------
    // All Cards
    // --------------------------------------------------------

    if (categoryIndex == 0) {

        if (cardCount == 0) {
            return false;
        }

        currentCard = 0;

        return true;
    }


    const char* category =
        categoryNames[
            categoryIndex - 1
        ];


    for (
        int i = 0;
        i < cardCount;
        i++
    ) {

        if (
            stringsEqualIgnoreCase(
                cards[i].category,
                category
            )
        ) {

            currentCard = i;

            return true;
        }
    }


    return false;
}


// Changes current category
static void selectCategory(
    int categoryIndex
) {

    if (
        categoryIndex < 0 ||
        categoryIndex > categoryCount
    ) {
        return;
    }


    showingFavourites = false;


    currentCategory =
        categoryIndex;


    if (
        selectFirstCardInCategory(
            categoryIndex
        )
    ) {

        showingDailyCard = false;
    }
}


// Switches to the favourites
static void selectFavourites() {

    if (!hasFavourites()) {
        return;
    }


    showingFavourites = true;


    chooseCard();
}


// Adds or removes the current card from favourites and saves to SD card
static void toggleFavourite() {

    if (
        currentCard < 0 ||
        currentCard >= cardCount
    ) {
        return;
    }


    favourites[currentCard] =
        !favourites[currentCard];


    saveFavourites();


    if (
        showingFavourites &&
        !hasFavourites()
    ) {

        showingFavourites = false;

        currentCategory = 0;

        chooseDailyCard();
    }
}


static void prepareText(
    C2D_Text* text,
    const char* string
) {

    C2D_TextFontParse(
        text,
        systemFont,
        textBuffer,
        string
    );

    C2D_TextOptimize(
        text
    );
}


static const char* initGraphics() {

    if (
        !C2D_Init(
            C2D_DEFAULT_MAX_OBJECTS
        )
    ) {
        return "C2D_Init failed.";
    }


    C2D_Prepare();


    topTarget =
        C2D_CreateScreenTarget(
            GFX_TOP,
            GFX_LEFT
        );


    if (topTarget == NULL) {
        return "Could not create the top-screen C2D target.";
    }


    bottomTarget =
        C2D_CreateScreenTarget(
            GFX_BOTTOM,
            GFX_LEFT
        );


    if (bottomTarget == NULL) {
        return "Could not create the bottom-screen C2D target.";
    }

    systemFont = NULL;


    textBuffer =
        C2D_TextBufNew(
            8192
        );


    if (textBuffer == NULL) {
        return "C2D_TextBufNew failed.";
    }


    return NULL;
}


static void wrapText(
    const char* source,
    char* destination,
    size_t destinationSize,
    int maxCharsPerLine
) {

    if (
        source == NULL ||
        destination == NULL ||
        destinationSize == 0 ||
        maxCharsPerLine <= 0
    ) {
        return;
    }


    size_t output = 0;

    int lineLength = 0;

    const char* current = source;


    while (
        *current != '\0' &&
        output + 1 < destinationSize
    ) {

        if (*current == '\n') {

            destination[output++] = '\n';

            current++;

            lineLength = 0;

            continue;
        }


        while (
            *current == ' ' &&
            lineLength == 0
        ) {
            current++;
        }


        if (*current == '\0') {
            break;
        }


        const char* wordStart = current;

        while (
            *current != '\0' &&
            *current != ' ' &&
            *current != '\n'
        ) {
            current++;
        }


        int wordLength =
            (int)(current - wordStart);


        if (
            lineLength > 0 &&
            lineLength + 1 + wordLength > maxCharsPerLine
        ) {

            if (
                output + 1 >= destinationSize
            ) {
                break;
            }

            destination[output++] = '\n';

            lineLength = 0;
        }


        if (
            lineLength > 0
        ) {

            if (
                output + 1 >= destinationSize
            ) {
                break;
            }

            destination[output++] = ' ';

            lineLength++;
        }


        for (
            int i = 0;
            i < wordLength &&
            output + 1 < destinationSize;
            i++
        ) {

            destination[output++] =
                wordStart[i];

            lineLength++;
        }


        if (*current == '\n') {

            if (
                output + 1 < destinationSize
            ) {

                destination[output++] = '\n';

                lineLength = 0;
            }

            current++;
        }
        else {

            while (*current == ' ') {
                current++;
            }
        }
    }


    destination[output] = '\0';
}

static void updateTextObjects() {

    static char wrappedAdvice[
        MAX_ADVICE_LENGTH + 128
    ];


    if (
        cardCount <= 0 ||
        currentCard < 0 ||
        currentCard >= cardCount
    ) {

        prepareText(
            &titleText,
            "No cards available."
        );

        prepareText(
            &categoryText,
            ""
        );

        prepareText(
            &adviceText,
            ""
        );

        prepareText(
            &favouriteText,
            ""
        );

        return;
    }


    prepareText(
        &categoryText,
        cards[currentCard].category
    );


    static char wrappedTitle[
        MAX_TITLE_LENGTH + 32
    ];


    wrapText(
        cards[currentCard].title,
        wrappedTitle,
        sizeof(wrappedTitle),
        32
    );


    prepareText(
        &titleText,
        wrappedTitle
    );


    wrapText(
        cards[currentCard].advice,
        wrappedAdvice,
        sizeof(wrappedAdvice),
        48
    );


    prepareText(
        &adviceText,
        wrappedAdvice
    );


    if (
        favourites[currentCard]
    ) {

        prepareText(
            &favouriteText,
            "FAVOURITE"
        );

    }
    else {

        prepareText(
            &favouriteText,
            ""
        );
    }
}

static void drawTopScreen() {

    C2D_TargetClear(
        topTarget,
        COLOR_BACKGROUND
    );


    C2D_SceneBegin(
        topTarget
    );

    C2D_DrawRectSolid(
        0,
        0,
        0,
        400,
        28,
        COLOR_PANEL
    );


    static C2D_Text headerText;


    prepareText(
        &headerText,
        "BPD3DS"
    );


    C2D_DrawText(
        &headerText,
        C2D_AlignCenter | C2D_WithColor,
        200,
        5,
        0,
        0.55f,
        0.55f,
        COLOR_TEXT
    );

    C2D_DrawText(
        &categoryText,
        C2D_AlignCenter | C2D_WithColor,
        200,
        30,
        0,
        0.58f,
        0.58f,
        COLOR_ACCENT
    );

    C2D_DrawText(
        &titleText,
        C2D_AlignCenter | C2D_WithColor,
        200,
        46, 
        0,
        0.50f,
        0.55f,
        COLOR_TEXT
    );


    C2D_DrawRectSolid(
        12,
        68,
        0,
        376,
        150,
        COLOR_PANEL
    );


    C2D_DrawText(
        &adviceText,
        C2D_AlignCenter | C2D_WithColor,
        200,
        78,
        0,
        0.50f,
        0.50f,
        COLOR_TEXT
    );


    if (
        currentCard >= 0 &&
        currentCard < cardCount &&
        favourites[currentCard]
    ) {

        C2D_DrawText(
            &favouriteText,
            C2D_AlignCenter | C2D_WithColor,
            200,
            219,
            0,
            0.40f,
            0.40f,
            COLOR_FAVOURITE
        );
    }


    static char positionString[64];

    int matchingIndexes[
        MAX_CARDS
    ];


    int matchingCount =
        getMatchingCards(
            matchingIndexes
        );


    int currentPosition = 0;


    for (
        int i = 0;
        i < matchingCount;
        i++
    ) {

        if (
            matchingIndexes[i]
            ==
            currentCard
        ) {

            currentPosition =
                i + 1;

            break;
        }
    }


    snprintf(
        positionString,
        sizeof(positionString),
        "%d / %d",
        currentPosition,
        matchingCount
    );


    static C2D_Text positionText;


    prepareText(
        &positionText,
        positionString
    );


    C2D_DrawText(
        &positionText,
        C2D_AlignRight | C2D_WithColor,
        390,
        222,
        0,
        0.35f,
        0.35f,
        COLOR_SECONDARY
    );
}


static void buildButtonText(
    int buttonIndex,
    const char* text
) {

    if (
        buttonIndex < 0 ||
        buttonIndex >= MAX_CATEGORIES + 2
    ) {
        return;
    }


    prepareText(
        &buttonTexts[buttonIndex],
        text
    );
}


static void drawBottomScreen() {

    C2D_TargetClear(
        bottomTarget,
        COLOR_BACKGROUND
    );


    C2D_SceneBegin(
        bottomTarget
    );


    C2D_DrawRectSolid(
        0,
        0,
        0,
        320,
        27,
        COLOR_PANEL
    );


    static C2D_Text headerText;


    prepareText(
        &headerText,
        "Choose Category"
    );


    C2D_DrawText(
        &headerText,
        C2D_AlignCenter | C2D_WithColor,
        160,
        5,
        0,
        0.50f,
        0.50f,
        COLOR_TEXT
    );


    float y =
        BUTTON_START_Y;


    u32 allButtonColour =
        (
            !showingFavourites &&
            currentCategory == 0
        )
        ?
        COLOR_BUTTON_SELECTED
        :
        COLOR_BUTTON;


    if (pressedButton == 0) {

        allButtonColour =
            COLOR_BUTTON_PRESSED;
    }


    C2D_DrawRectSolid(
        BUTTON_LEFT,
        y,
        0,
        BUTTON_RIGHT - BUTTON_LEFT,
        BUTTON_HEIGHT,
        allButtonColour
    );


    buildButtonText(
        0,
        "All Cards"
    );


    C2D_DrawText(
        &buttonTexts[0],
        C2D_AlignCenter | C2D_WithColor,
        160,
        y + 6,
        0,
        0.40f,
        0.40f,
        COLOR_TEXT
    );


    for (
        int i = 0;
        i < categoryCount;
        i++
    ) {

        y +=
            BUTTON_HEIGHT +
            BUTTON_GAP;


        int buttonIndex =
            i + 1;


        bool selected =
            (
                !showingFavourites &&
                currentCategory ==
                    buttonIndex
            );


        u32 buttonColour =
            selected
            ?
            COLOR_BUTTON_SELECTED
            :
            COLOR_BUTTON;


        if (
            pressedButton ==
            buttonIndex
        ) {

            buttonColour =
                COLOR_BUTTON_PRESSED;
        }


        C2D_DrawRectSolid(
            BUTTON_LEFT,
            y,
            0,
            BUTTON_RIGHT - BUTTON_LEFT,
            BUTTON_HEIGHT,
            buttonColour
        );


        buildButtonText(
            buttonIndex,
            categoryNames[i]
        );


        C2D_DrawText(
            &buttonTexts[buttonIndex],
            C2D_AlignCenter | C2D_WithColor,
            160,
            y + 6,
            0,
            0.40f,
            0.40f,
            COLOR_TEXT
        );
    }


    y +=
        BUTTON_HEIGHT +
        BUTTON_GAP;


    int favouritesButton =
        categoryCount + 1;


    u32 favouritesColour =
        showingFavourites
        ?
        COLOR_BUTTON_SELECTED
        :
        COLOR_BUTTON;


    if (
        pressedButton ==
        favouritesButton
    ) {

        favouritesColour =
            COLOR_BUTTON_PRESSED;
    }


    C2D_DrawRectSolid(
        BUTTON_LEFT,
        y,
        0,
        BUTTON_RIGHT - BUTTON_LEFT,
        BUTTON_HEIGHT,
        favouritesColour
    );


    buildButtonText(
        favouritesButton,
        "Favourites"
    );


    C2D_DrawText(
        &buttonTexts[favouritesButton],
        C2D_AlignCenter | C2D_WithColor,
        160,
        y + 6,
        0,
        0.40f,
        0.40f,
        COLOR_FAVOURITE
    );


    static C2D_Text footer;


    prepareText(
        &footer,
        "A Random   X Fav   Y Favs   B Help"
    );


    C2D_DrawText(
        &footer,
        C2D_AlignCenter | C2D_WithColor,
        160,
        222,
        0,
        0.36f,
        0.36f,
        COLOR_SECONDARY
    );

}


static void drawHelpScreen() {

    C2D_TargetClear(
        topTarget,
        COLOR_BACKGROUND
    );


    C2D_SceneBegin(
        topTarget
    );


    static C2D_Text helpTitle;


    prepareText(
        &helpTitle,
        "BPD SELF-HELP - HELP"
    );


    C2D_DrawText(
        &helpTitle,
        C2D_AlignCenter | C2D_WithColor,
        200,
        15,
        0,
        0.60f,
        0.60f,
        COLOR_TEXT
    );

    C2D_Text helpKeys;
    C2D_Text helpActions;

    prepareText(
        &helpKeys,
        "A\n"
        "Up/Down\n"
        "Left/Right\n"
        "X\n"
        "Y\n"
        "Touch\n"
        "B\n"
        "START"
    );

    prepareText(
        &helpActions,
        "New random card\n"
        "Change category\n"
        "Browse cards\n"
        "Favourite / Unfavourite\n"
        "Favourites / All\n"
        "Choose category\n"
        "Return to cards\n"
        "Exit"
    );

    C2D_DrawText(
        &helpKeys,
        C2D_WithColor,
        35,
        55,
        0,
        0.45f,
        0.45f,
        COLOR_ACCENT
    );

    C2D_DrawText(
        &helpActions,
        C2D_WithColor,
        125,
        55,
        0,
        0.45f,
        0.45f,
        COLOR_TEXT
    );


    C2D_TargetClear(
        bottomTarget,
        COLOR_PANEL
    );


    C2D_SceneBegin(
        bottomTarget
    );


    static C2D_Text aboutTitle;


    prepareText(
        &aboutTitle,
        "ABOUT THIS APP"
    );


    C2D_DrawText(
        &aboutTitle,
        C2D_AlignCenter | C2D_WithColor,
        160,
        15,
        0,
        0.55f,
        0.55f,
        COLOR_ACCENT
    );


    static C2D_Text aboutText;


    prepareText(
        &aboutText,
        "A portable 3DS version of The BPD Card Deck,\n"
        "with practical tools and exercises for managing\n"
        "BPD symptoms.\n\n"
        "The original 52-card deck was created by\n"
        "Daniel J. Fox, PhD, a psychologist specialising\n"
        "in personality disorders.\n\n"
        "The BPD Card Deck - Daniel J. Fox, PhD\n"
        "New Harbinger Publications, 2023.\n\n"
        "This is an unofficial app and is not affiliated\n"
        "with or endorsed by the author or publisher.\n\n"
        "Press B to return."
    );


    C2D_DrawText(
        &aboutText,
        C2D_AlignCenter | C2D_WithColor,
        160,
        30,
        0,
        0.42f,
        0.42f,
        COLOR_TEXT
    );
}


static void drawLoadError() {

    C2D_TargetClear(
        topTarget,
        COLOR_BACKGROUND
    );


    C2D_SceneBegin(
        topTarget
    );


    static C2D_Text errorTitle;


    prepareText(
        &errorTitle,
        "COULD NOT LOAD BPDCardDeck.txt"
    );


    C2D_DrawText(
        &errorTitle,
        C2D_AlignCenter | C2D_WithColor,
        200,
        30,
        0,
        0.55f,
        0.55f,
        COLOR_TEXT
    );


    static C2D_Text errorText;


    prepareText(
        &errorText,
        "Make sure BPDCardDeck.txt exists\n"
        "in the project's romfs folder.\n\n"
        "Format:\n"
        "Category|Title|Advice\n\n"
        "Press START to exit."
    );


    C2D_DrawText(
        &errorText,
        C2D_AlignCenter | C2D_WithColor,
        200,
        75,
        0,
        0.45f,
        0.45f,
        COLOR_TEXT
    );


    C2D_TargetClear(
        bottomTarget,
        COLOR_BACKGROUND
    );


    C2D_SceneBegin(
        bottomTarget
    );
}

static void redraw() {

}

static int getTouchedButton(
    const touchPosition* touch
) {

    if (touch == NULL) {
        return -1;
    }


    if (
        touch->px < BUTTON_LEFT ||
        touch->px > BUTTON_RIGHT
    ) {
        return -1;
    }


    float y =
        BUTTON_START_Y;


    if (
        touch->py >= y &&
        touch->py <=
            y + BUTTON_HEIGHT
    ) {

        return 0;
    }

    for (
        int i = 0;
        i < categoryCount;
        i++
    ) {

        y +=
            BUTTON_HEIGHT +
            BUTTON_GAP;


        if (
            touch->py >= y &&
            touch->py <=
                y + BUTTON_HEIGHT
        ) {

            return i + 1;
        }
    }

    y +=
        BUTTON_HEIGHT +
        BUTTON_GAP;


    if (
        touch->py >= y &&
        touch->py <=
            y + BUTTON_HEIGHT
    ) {

        return categoryCount + 1;
    }


    return -1;
}


static void handleTouch(
    bool* touchWasHandled
) {

    touchPosition touch;


    hidTouchRead(
        &touch
    );


    bool touching =
        touch.px != 0 ||
        touch.py != 0;


    if (showingHelp) {

        pressedButton = -1;

        if (!touching) {
            *touchWasHandled = false;
        }

        return;
    }


    if (!touching) {

        pressedButton = -1;

        *touchWasHandled = false;

        return;
    }


    int button =
        getTouchedButton(
            &touch
        );


    if (
        button >= 0
    ) {

        pressedButton = button;

        redraw();
    }



    if (
        button >= 0 &&
        !*touchWasHandled
    ) {

        if (
            button == categoryCount + 1
        ) {

            if (
                hasFavourites()
            ) {

                selectFavourites();
            }

        }
        else {


            selectCategory(
                button
            );
        }


        *touchWasHandled = true;

        pressedButton = -1;

        redraw();
    }
}


static void changeCategory(
    int direction
) {

    if (
        categoryCount <= 0 ||
        direction == 0
    ) {
        return;
    }


    int favouritesIndex =
        categoryCount + 1;


    int currentSelection =
        showingFavourites
        ?
        favouritesIndex
        :
        currentCategory;


    int nextSelection =
        currentSelection + direction;


    while (true) {

        if (
            nextSelection < 0
        ) {
            nextSelection =
                favouritesIndex;
        }


        if (
            nextSelection > favouritesIndex
        ) {
            nextSelection = 0;
        }


        if (
            nextSelection == favouritesIndex &&
            !hasFavourites()
        ) {

            nextSelection += direction;

            continue;
        }


        break;
    }


    if (
        nextSelection == favouritesIndex
    ) {

        selectFavourites();
    }
    else {

        selectCategory(
            nextSelection
        );
    }
}


static void handleKeys(
    u32 pressedKeys
) {


    if (showingHelp) {

        if (
            pressedKeys & KEY_B
        ) {

            showingHelp = false;

            redraw();
        }

        return;
    }



    if (
        pressedKeys & KEY_B
    ) {

        showingHelp = true;

        redraw();

        return;
    }


    bool changed = false;


    if (
        pressedKeys & KEY_X
    ) {

        toggleFavourite();

        changed = true;
    }



    if (
        pressedKeys & KEY_Y
    ) {

        if (showingFavourites) {

            showingFavourites = false;

            currentCategory = 0;

            chooseDailyCard();

        }
        else {

            if (
                hasFavourites()
            ) {

                showingFavourites = true;

                chooseCard();
            }
        }


        changed = true;
    }


    if (
        pressedKeys & KEY_UP
    ) {

        changeCategory(-1);

        changed = true;
    }


    if (
        pressedKeys & KEY_DOWN
    ) {

        changeCategory(1);

        changed = true;
    }


    if (
        pressedKeys & KEY_LEFT
    ) {

        browseCards(-1);

        changed = true;
    }


    if (
        pressedKeys & KEY_RIGHT
    ) {

        browseCards(1);

        changed = true;
    }

    if (
        pressedKeys & KEY_A
    ) {

        chooseCard();

        changed = true;
    }


    if (changed) {
        redraw();
    }
}


int main(void) {


    gfxInitDefault();


    if (
        !C3D_Init(
            C3D_DEFAULT_CMDBUF_SIZE
        )
    ) {

        showStartupError(
            "C3D_Init failed."
        );

        gfxExit();

        return 1;
    }


    const char* graphicsError =
        initGraphics();


    if (graphicsError != NULL) {

        showStartupError(
            graphicsError
        );

        C2D_Fini();
        C3D_Fini();
        gfxExit();

        return 1;
    }


    Result romfsResult =
        romfsInit();


    if (
        R_FAILED(
            romfsResult
        )
    ) {

        char errorMessage[128];

        snprintf(
            errorMessage,
            sizeof(errorMessage),
            "romfsInit failed.\nResult: 0x%08lX",
            (unsigned long)romfsResult
        );

        showStartupError(
            errorMessage
        );

        if (textBuffer != NULL) {
            C2D_TextBufDelete(textBuffer);
            textBuffer = NULL;
        }

        C2D_Fini();
        C3D_Fini();
        gfxExit();

        return 1;
    }


    if (
        !loadCards()
    ) {

        while (
            aptMainLoop()
        ) {

            hidScanInput();

            if (
                hidKeysDown() & KEY_START
            ) {
                break;
            }

            C3D_FrameBegin(
                C3D_FRAME_SYNCDRAW
            );

            C2D_TextBufClear(
                textBuffer
            );

            drawLoadError();

            C3D_FrameEnd(
                0
            );
        }

        romfsExit();

        if (textBuffer != NULL) {
            C2D_TextBufDelete(textBuffer);
            textBuffer = NULL;
        }

        C2D_Fini();
        C3D_Fini();
        gfxExit();

        return 1;
    }


    srand(
        (unsigned int)osGetTime()
    );


    loadFavourites();


    chooseDailyCard();


    bool touchWasHandled =
        false;


    while (
        aptMainLoop()
    ) {

        hidScanInput();

        u32 pressedKeys =
            hidKeysDown();


        if (
            pressedKeys & KEY_START
        ) {
            break;
        }


        handleKeys(
            pressedKeys
        );


        handleTouch(
            &touchWasHandled
        );


        C3D_FrameBegin(
            C3D_FRAME_SYNCDRAW
        );


        C2D_TextBufClear(
            textBuffer
        );


        updateTextObjects();


        if (showingHelp) {

            drawHelpScreen();

        }
        else {

            drawTopScreen();

            drawBottomScreen();
        }


        C3D_FrameEnd(
            0
        );
    }


    if (textBuffer != NULL) {

        C2D_TextBufDelete(
            textBuffer
        );

        textBuffer = NULL;
    }


    C2D_Fini();

    C3D_Fini();

    romfsExit();

    gfxExit();

    return 0;
}

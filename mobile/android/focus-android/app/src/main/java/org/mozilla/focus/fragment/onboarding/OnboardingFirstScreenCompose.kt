/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.fragment.onboarding

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.Button
import androidx.compose.material.ButtonDefaults
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.colorResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import mozilla.components.ui.colors.PhotonColors
import org.mozilla.focus.R
import org.mozilla.focus.ui.theme.FocusTheme
import org.mozilla.focus.ui.theme.focusTypography
import org.mozilla.focus.ui.theme.gradientBackground

@Composable
@Preview
private fun OnBoardingFirstScreenComposePreview() {
    FocusTheme {
        OnBoardingFirstScreenCompose {}
    }
}

/**
 * Displays the first onBoarding screen
 *
 * @param onGetStartedButtonClicked Will be called when the user clicks on get started button.
 */
@Composable
fun OnBoardingFirstScreenCompose(
    onGetStartedButtonClicked: () -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .gradientBackground(),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Image(
            painter = painterResource(R.drawable.onboarding_logo),
            contentDescription = LocalContext.current.getString(R.string.app_name),
            modifier = Modifier
                .size(150.dp, 150.dp),
        )
        Text(
            text = stringResource(
                R.string.onboarding_first_screen_title,
                stringResource(R.string.app_name),
            ),
            modifier = Modifier
                .padding(top = 32.dp, start = 16.dp, end = 16.dp),
            textAlign = TextAlign.Center,
            style = focusTypography.onboardingTitle,
        )
        Text(
            text = stringResource(
                R.string.onboarding_first_screen_subtitle,
            ),
            modifier = Modifier
                .padding(top = 16.dp, start = 16.dp, end = 16.dp),
            textAlign = TextAlign.Center,
            style = focusTypography.onboardingSubtitle,
        )
        ComponentGoToOnBoardingSecondScreen {
            onGetStartedButtonClicked()
        }
    }
}

@Composable
private fun ComponentGoToOnBoardingSecondScreen(goToOnBoardingSecondScreen: () -> Unit) {
    Button(
        onClick = goToOnBoardingSecondScreen,
        modifier = Modifier
            .padding(top = 40.dp, start = 16.dp, end = 16.dp)
            .fillMaxWidth(),
        colors = ButtonDefaults.textButtonColors(
            backgroundColor = colorResource(R.color.onboardingButtonOneColor),
        ),
    ) {
        Text(
            text = AnnotatedString(
                LocalContext.current.resources.getString(
                    R.string.onboarding_first_screen_button_text,
                ),
            ),
            color = PhotonColors.White,
        )
    }
}
